#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace owned {

enum class PlacedHitchFinishReason : std::uint8_t {
    none,
    complete,
    truncated,
    invalid_clock,
    clock_discontinuity,
    focus_lost,
    no_swap_chain,
    native_flush_failed,
    ui_flush_failed,
    present_failed,
    trim_failed,
    world_transition,
    scene_partition_invalid,
    native_depth_partition_invalid,
    renderer_release,
};

inline const char* placed_hitch_finish_reason_name(
    PlacedHitchFinishReason reason) noexcept {
    switch (reason) {
    case PlacedHitchFinishReason::none: return "none";
    case PlacedHitchFinishReason::complete: return "complete";
    case PlacedHitchFinishReason::truncated: return "truncated";
    case PlacedHitchFinishReason::invalid_clock: return "invalid_clock";
    case PlacedHitchFinishReason::clock_discontinuity: return "clock_discontinuity";
    case PlacedHitchFinishReason::focus_lost: return "focus_lost";
    case PlacedHitchFinishReason::no_swap_chain: return "no_swap_chain";
    case PlacedHitchFinishReason::native_flush_failed: return "native_flush_failed";
    case PlacedHitchFinishReason::ui_flush_failed: return "ui_flush_failed";
    case PlacedHitchFinishReason::present_failed: return "present_failed";
    case PlacedHitchFinishReason::trim_failed: return "trim_failed";
    case PlacedHitchFinishReason::world_transition: return "world_transition";
    case PlacedHitchFinishReason::scene_partition_invalid:
        return "scene_partition_invalid";
    case PlacedHitchFinishReason::native_depth_partition_invalid:
        return "native_depth_partition_invalid";
    case PlacedHitchFinishReason::renderer_release: return "renderer_release";
    }
    return "unknown";
}

struct PlacedHitchEngineFrame {
    double frame_cpu_ms = 0.0;
    double outside_scene_interval_ms = 0.0;
    double outside_entry_cpu_ms = 0.0;
    double world_draw_cpu_ms = 0.0;
    double world_unattributed_cpu_ms = 0.0;
    double scene_3d_cpu_ms = 0.0;
    double scene_3d_unattributed_cpu_ms = 0.0;
    double scene_partition_retained_nonqueue_cpu_ms = 0.0;
    std::array<double,12> city_retained_owner_cpu_ms{};
    double scene_partition_object_nonqueue_cpu_ms = 0.0;
    double scene_partition_queue_native_flush_cpu_ms = 0.0;
    double scene_partition_queue_ui_flush_cpu_ms = 0.0;
    double scene_partition_queue_packet_or_control_cpu_ms = 0.0;
    double scene_partition_remaining_game_cpu_ms = 0.0;
    double outside_scene_3d_cpu_ms = 0.0;
    double object_draw_cpu_ms = 0.0;
    double object_draw_unattributed_cpu_ms = 0.0;
    double object_plan_bootstrap_cpu_ms = 0.0;
    double object_plan_submit_cpu_ms = 0.0;
    double simple_cpu_ms = 0.0;
    double figure_placed_cpu_ms = 0.0;
    double figure_lit_cpu_ms = 0.0;
    double figure_lit_morphed_cpu_ms = 0.0;
    double figure_lit_rigid_cpu_ms = 0.0;
    double terrain_cpu_ms = 0.0;
    double terrain_outer_cpu_ms = 0.0;
    double terrain_capture_cpu_ms = 0.0;
    double terrain_submit_cpu_ms = 0.0;

    std::uint64_t world_draw_calls = 0;
    std::uint64_t scene_3d_calls = 0;
    std::uint64_t scene_partition_calls = 0;
    std::uint64_t scene_partition_retained_calls = 0;
    std::uint64_t scene_partition_object_outer_calls = 0;
    std::uint64_t scene_partition_queue_calls = 0;
    std::uint64_t object_draw_calls = 0;
    std::uint64_t object_plan_original_calls = 0;
    std::uint64_t object_plan_owned_calls = 0;
    std::uint64_t object_plan_submitted_figures = 0;
    std::uint64_t object_plan_early_omitted_calls = 0;
    std::uint64_t simple_calls = 0;
    std::uint64_t figure_placed_calls = 0;
    std::uint64_t figure_lit_calls = 0;
    std::uint64_t figure_gpu_owned_calls = 0;
    std::uint64_t figure_cpu_fallback_calls = 0;
    std::uint64_t figure_gpu_submit_failures = 0;
    std::uint64_t terrain_calls = 0;
    std::uint64_t terrain_outer_calls = 0;
    std::uint64_t terrain_gpu_owned_calls = 0;
    std::uint64_t terrain_cpu_fallback_calls = 0;
    std::uint64_t terrain_gpu_submit_failures = 0;
    std::uint64_t terrain_spatial_bounds_prepared = 0;
    std::uint64_t terrain_spatial_bounds_reused = 0;
    bool scene_partition_valid = false;
};

struct PlacedHitchShadowDecision {
    std::uint32_t begin_calls = 0;
    std::uint8_t previous_valid_mask = 0, previous_enabled_mask = 0;
    std::uint8_t current_enabled_mask = 0;
    std::uint8_t previous_spatial_mask = 0, current_spatial_mask = 0;
    std::uint8_t numeric_compared_mask = 0, numeric_changed_mask = 0;
    // First differing numeric component in the existing comparison order.
    std::uint8_t light_transform_changed_mask = 0, camera_slice_changed_mask = 0;
    std::uint8_t bias_changed_mask = 0, receiver_bounds_changed_mask = 0;
    std::uint8_t validity_compared_mask = 0, validity_changed_mask = 0;
    std::uint8_t dirty_mask = 0, source_dirty_mask = 0;
    std::array<std::uint64_t,5> previous_validity_counts{};
    std::array<std::uint64_t,5> current_validity_counts{};
    // A part can contribute to several slices. These counts are not additive.
    std::array<std::uint64_t,5> selected_per_slice{};
    std::uint64_t source_publications = 0;
    std::uint64_t source_changed_visuals = 0, source_changed_terrain = 0;
    std::uint64_t source_receiver_refits = 0, source_caster_refits = 0;
    std::uint64_t source_map_reuses = 0;
    std::uint64_t value_reuses = 0, frame_copies = 0;
};

struct PlacedHitchRendererFrame {
    std::uint64_t logical_draws = 0;
    std::uint64_t physical_batches = 0;
    std::uint64_t texture_array_attempts = 0;
    std::uint64_t texture_array_successes = 0;
    std::uint64_t texture_array_unavailable = 0;
    std::uint64_t texture_array_format_failures = 0;
    std::uint64_t texture_array_layout_failures = 0;
    std::uint64_t texture_array_allocation_failures = 0;
    std::uint64_t terrain_topology_uploads = 0;
    std::uint64_t terrain_topology_upload_vertices = 0;
    std::uint64_t terrain_color_uploads = 0;
    std::uint64_t terrain_color_upload_vertices = 0;
    std::uint64_t native_depth_selected = 0;
    std::uint64_t native_depth_warm = 0;
    std::uint64_t native_depth_cold = 0;
    std::uint64_t native_depth_registration_callbacks = 0;
    std::uint64_t native_shadow_draws = 0;
    std::uint64_t native_shadow_triangles = 0;
    bool native_shadows_enabled = true;
    std::uint64_t native_depth_cold_handle_owner_identity = 0;
    std::uint64_t native_depth_cold_source_revision = 0;
    std::uint64_t native_depth_cold_inherited_context = 0;
    std::uint64_t native_depth_cold_caster_revision = 0;
    std::uint64_t native_depth_cold_world_allocation_generation = 0;
    std::uint64_t native_depth_cold_pose_revision_generation_allocation = 0;
    std::uint64_t native_depth_cold_animation_generation_ownership = 0;
    std::uint64_t native_depth_cold_bounds = 0;
    PlacedHitchShadowDecision shadow_decision{};
    struct ColorResourceDiagnostic {
        std::int64_t total_ticks = 0;
        std::int64_t cold_registration_ticks = 0;
        std::int64_t texture_ticks = 0;
        std::int64_t texture_create_ticks = 0;
        std::int64_t mesh_ticks = 0;
        std::int64_t geometry_upload_ticks = 0;
        std::int64_t arena_ticks = 0;
        std::int64_t nonmesh_geometry_upload_ticks = 0;
        std::int64_t nonmesh_arena_ticks = 0;
        std::int64_t flush_ticks = 0;
        std::int64_t mesh_flush_ticks = 0;
        std::int64_t texture_flush_ticks = 0;
        std::int64_t cold_flush_ticks = 0;
        std::uint64_t tagged_calls = 0;
        std::uint64_t warm_parts = 0;
        std::uint64_t cold_parts = 0;
        std::uint64_t texture_cold_calls = 0;
        std::uint64_t texture_upload_bytes = 0;
        std::uint64_t mesh_cold_calls = 0;
        std::uint64_t geometry_upload_bytes = 0;
        std::uint64_t arena_grows = 0;
        std::uint64_t arena_copy_bytes = 0;
        std::uint64_t failed_calls = 0;
    } color_resource{};
    double color_tick_to_ms = 0.0;
    std::uint64_t gpu_snapshot_frame = 0;
    double gpu_snapshot_cpu_ms = 0.0;
    double gpu_snapshot_ms = 0.0;
    bool gpu_snapshot_available = false;
    bool gpu_snapshot_valid = false;
};

inline bool placed_hitch_color_partition_valid(
    const PlacedHitchRendererFrame::ColorResourceDiagnostic& color) noexcept {
    if (color.total_ticks < 0 || color.cold_registration_ticks < 0 ||
        color.cold_flush_ticks < 0 || color.texture_ticks < 0 ||
        color.texture_create_ticks < 0 || color.mesh_ticks < 0 ||
        color.geometry_upload_ticks < 0 || color.arena_ticks < 0 ||
        color.nonmesh_geometry_upload_ticks < 0 || color.nonmesh_arena_ticks < 0 ||
        color.flush_ticks < 0 || color.mesh_flush_ticks < 0 ||
        color.texture_flush_ticks < 0) return false;
    if (color.nonmesh_geometry_upload_ticks > color.geometry_upload_ticks ||
        color.nonmesh_arena_ticks > color.arena_ticks) return false;
    const auto mesh_upload = color.geometry_upload_ticks - color.nonmesh_geometry_upload_ticks;
    const auto mesh_arena = color.arena_ticks - color.nonmesh_arena_ticks;
    const auto nonmesh_resource = color.nonmesh_geometry_upload_ticks + color.nonmesh_arena_ticks;
    if (color.texture_create_ticks > color.texture_ticks ||
        color.texture_flush_ticks > color.texture_ticks - color.texture_create_ticks ||
        mesh_upload > color.mesh_ticks ||
        mesh_arena > color.mesh_ticks - mesh_upload ||
        color.mesh_flush_ticks > color.mesh_ticks - mesh_upload - mesh_arena ||
        color.mesh_flush_ticks > color.flush_ticks ||
        color.texture_flush_ticks > color.flush_ticks - color.mesh_flush_ticks)
        return false;
    const auto outside_flush = color.flush_ticks - color.mesh_flush_ticks - color.texture_flush_ticks;
    if (color.cold_flush_ticks < color.mesh_flush_ticks + color.texture_flush_ticks ||
        color.cold_flush_ticks > color.flush_ticks ||
        color.texture_ticks > color.cold_registration_ticks ||
        color.mesh_ticks > color.cold_registration_ticks - color.texture_ticks ||
        nonmesh_resource > color.cold_registration_ticks - color.texture_ticks - color.mesh_ticks ||
        color.cold_flush_ticks - color.mesh_flush_ticks - color.texture_flush_ticks >
            color.cold_registration_ticks - color.texture_ticks - color.mesh_ticks - nonmesh_resource)
        return false;
    return color.cold_registration_ticks <= color.total_ticks &&
        color.flush_ticks - color.cold_flush_ticks <=
            color.total_ticks - color.cold_registration_ticks &&
        color.texture_ticks <= color.total_ticks &&
        color.mesh_ticks <= color.total_ticks - color.texture_ticks &&
        nonmesh_resource <= color.total_ticks - color.texture_ticks - color.mesh_ticks &&
        outside_flush <= color.total_ticks - color.texture_ticks - color.mesh_ticks - nonmesh_resource;
}

inline bool placed_hitch_native_depth_partition_valid(
    const PlacedHitchRendererFrame& frame) noexcept {
    if (frame.native_depth_warm > frame.native_depth_selected ||
        frame.native_depth_cold !=
            frame.native_depth_selected - frame.native_depth_warm ||
        frame.native_depth_registration_callbacks != frame.native_depth_cold)
        return false;
    std::uint64_t remaining = frame.native_depth_cold;
    const std::uint64_t reasons[] = {
        frame.native_depth_cold_handle_owner_identity,
        frame.native_depth_cold_source_revision,
        frame.native_depth_cold_inherited_context,
        frame.native_depth_cold_caster_revision,
        frame.native_depth_cold_world_allocation_generation,
        frame.native_depth_cold_pose_revision_generation_allocation,
        frame.native_depth_cold_animation_generation_ownership,
        frame.native_depth_cold_bounds,
    };
    for (const auto value : reasons) {
        if (value > remaining) return false;
        remaining -= value;
    }
    return remaining == 0;
}

inline constexpr std::size_t kPlacedHitchReasonCapacity = 96;
inline constexpr std::size_t kPlacedHitchStatusCapacity = 32;

struct PlacedHitchAppearanceDiagnostic {
    double model_native_ms = 0.0;
    double model_owned_ms = 0.0;
    double model_provenance_ms = 0.0;
    double model_resolve_other_ms = 0.0;
    double model_file_read_ms = 0.0;
    double model_comparison_ms = 0.0;
    double model_owned_other_ms = 0.0;
    double model_archive_ms = 0.0;
    double asset_lookup_ms = 0.0;
    double asset_decode_ms = 0.0; // Nested within asset_lookup_ms.
    double material_ms = 0.0;
    double texture_ms = 0.0;
    double publication_ms = 0.0;
    double remainder_ms = 0.0;
    std::uint64_t model_acquisitions = 0;
    std::uint64_t model_selected_bytes = 0;
    std::uint64_t model_unique_identities = 0;
    std::uint64_t model_repeated_identities = 0;
    std::uint64_t model_repeated_selected_bytes = 0;
    std::uint64_t model_file_read_bytes = 0;
    std::uint64_t model_file_read_calls = 0;
    std::uint64_t model_repeated_file_read_bytes = 0;
    std::uint64_t model_backing_cache_hits = 0;
    std::uint64_t model_comparison_span_bytes = 0;
    std::uint64_t model_repeated_comparison_span_bytes = 0;
    std::uint64_t skeleton_cache_hits = 0;
    std::uint64_t skeleton_cache_misses = 0;
    std::uint64_t figure_cache_hits = 0;
    std::uint64_t figure_cache_misses = 0;
    std::uint64_t decoded_figure_bytes = 0;
    bool available = false;
    bool accounting_valid = false;
    bool identity_complete = false;
    bool mixed_resolver_generation = false;
    bool resource_partition_valid = false;
    bool selected_identity_overflow = false;
    std::uint8_t selected_identity_count = 0;
};

struct PlacedHitchVisualFinish {
    double finish_cpu_ms = 0.0;
    double appearance_cpu_ms = 0.0;
    double selected_clip_cpu_ms = 0.0;
    double outer_unit_values_cpu_ms = 0.0;
    double other_cpu_ms = 0.0;
    std::int32_t signed_id = 0;
    std::uint64_t resource_serial = 0;
    std::uint64_t appearance_calls = 0;
    std::uint64_t selected_clip_calls = 0;
    std::uint64_t outer_unit_values_calls = 0;
    std::uint32_t failure_status = 0;
    bool has_scope = false;
    bool completion_succeeded = false;
    bool appearance_attempted = false;
    bool appearance_succeeded = false;
    bool appearance_changed = false;
    bool appearance_missing_assembly = false;
    PlacedHitchAppearanceDiagnostic appearance_diagnostic{};
    std::array<char, kPlacedHitchReasonCapacity> failure_reason{};
    std::array<char, kPlacedHitchReasonCapacity> appearance_reason{};
    std::array<char, kPlacedHitchStatusCapacity> failure_status_name{};
};

struct PlacedHitchDirectCall {
    std::int64_t original_ticks = 0;
    std::int64_t unit_original_ticks = 0;
    std::int64_t unit_finish_ticks = 0;
    std::uint64_t calls = 0;
    double original_cpu_ms = 0.0;
    double unit_original_cpu_ms = 0.0;
    double unit_finish_cpu_ms = 0.0;
    double exclusive_cpu_ms = 0.0;
};

struct PlacedHitchNativeOwnedCallback {
    std::int64_t exclusive_ticks = 0;
    std::uint64_t calls = 0;
    double exclusive_cpu_ms = 0.0;
    double mean_cpu_ms = 0.0;
};

constexpr std::size_t kPlacedHitchVisualEventCapacity = 64;
struct PlacedHitchVisualEvent {
    std::uint64_t sequence = 0;
    std::uint64_t serial = 0;
    double exclusive_cpu_ms = 0.0;
    std::int32_t signed_id = 0;
    std::uint32_t source_mask = 0;
    std::uint16_t change_mask = 0;
    std::uint8_t kind = 0, callback = 0, phase = 0;
    bool assembly_before = false, assembly_after = false, succeeded = false;
};

struct PlacedHitchFrame {
    std::uint64_t presentation_frame = 0;
    std::uint64_t bridge_frame = 0;
    std::uint64_t world_epoch = 0;
    double interval_ms = 0.0;
    double renderer_present_entry_to_present_return_ms = 0.0;
    double count_time_after_clock_ms = 0.0;
    double interface_on_idle_ms = 0.0;
    double message_handling_ms = 0.0;
    double post_end_frame_to_present_entry_ms = 0.0;
    double previous_post_present_trim_ms = 0.0;
    double visual_unit_update_original_cpu_ms = 0.0;
    double visual_unit_update_finish_cpu_ms = 0.0;
    double visual_unit_update_finish_appearance_cpu_ms = 0.0;
    double visual_unit_update_finish_selected_clip_cpu_ms = 0.0;
    double visual_unit_update_finish_outer_unit_values_cpu_ms = 0.0;
    double visual_unit_update_finish_other_cpu_ms = 0.0;
    double native_lifecycle_tick_original_cpu_ms = 0.0;
    double native_lifecycle_tick_unit_original_cpu_ms = 0.0;
    double native_lifecycle_tick_unit_finish_cpu_ms = 0.0;
    double native_lifecycle_tick_exclusive_cpu_ms = 0.0;
    std::array<PlacedHitchNativeOwnedCallback,6> native_tick_owned_callbacks{};
    std::array<PlacedHitchVisualEvent,kPlacedHitchVisualEventCapacity> visual_events{};
    std::uint32_t visual_event_count = 0, visual_event_overflow = 0;
    bool visual_events_complete = false;
    double native_lifecycle_tick_unassigned_cpu_ms = 0.0;
    // Callsite order: pre-tick, fixed-step, post-tick. These are positional
    // labels, not claims about gameplay or server ownership.
    std::array<PlacedHitchDirectCall, 3> count_time_direct_calls{};
    double count_time_outside_admitted_ticks_cpu_ms = 0.0;
    bool count_time_accounting_valid = false;
    double visual_world_tick_cpu_ms = 0.0;
    double visual_world_tick_objects_cpu_ms = 0.0;
    double visual_world_tick_terrain_cpu_ms = 0.0;
    double visual_world_tick_bridge_forced_cpu_ms = 0.0;
    double visual_world_tick_commit_cpu_ms = 0.0;
    double post_engine_surface_proxy_cpu_ms = 0.0;
    double post_engine_surface_flush_cpu_ms = 0.0;
    double post_engine_surface_cpu_rect_cpu_ms = 0.0;
    double post_engine_completed_surface_cpu_ms = 0.0;
    double partition_measured_ms = 0.0;
    double partition_residual_ms = 0.0;
    std::uint64_t count_time_calls = 0;
    std::uint64_t interface_on_idle_calls = 0;
    std::uint64_t message_handler_calls = 0;
    std::uint64_t visual_unit_update_original_calls = 0;
    std::uint64_t visual_unit_update_finish_calls = 0;
    std::uint64_t visual_unit_update_finish_appearance_calls = 0;
    std::uint64_t visual_unit_update_finish_selected_clip_calls = 0;
    std::uint64_t visual_unit_update_finish_outer_unit_values_calls = 0;
    std::uint64_t native_lifecycle_tick_original_calls = 0;
    std::uint64_t visual_world_tick_calls = 0;
    std::uint64_t visual_world_tick_manager_objects = 0;
    std::uint64_t visual_world_tick_terrain_layers = 0;
    std::uint64_t visual_world_tick_terrain_noise_values = 0;
    std::uint64_t visual_world_tick_bridge_planes = 0;
    std::uint64_t visual_world_tick_forced_ids = 0;
    std::uint64_t visual_world_tick_committed_entities = 0;
    std::uint64_t post_engine_surface_proxy_calls = 0;
    std::uint64_t post_engine_surface_flush_calls = 0;
    std::uint64_t post_engine_surface_cpu_rect_calls = 0;
    std::uint64_t post_engine_surface_cpu_rect_pixels = 0;
    std::uint64_t post_engine_completed_surface_calls = 0;
    std::uint64_t post_engine_completed_surface_bytes = 0;
    std::uint64_t post_engine_surface_material_group_full_scans = 0;
    PlacedHitchVisualFinish slowest_visual_unit_update_finish{};
    PlacedHitchEngineFrame engine{};
    PlacedHitchRendererFrame renderer{};
    bool focused = false;
    bool bridge_fresh = false;
    bool outer_timing_fresh = false;
    bool present_succeeded = false;
    bool previous_trim_succeeded = true;
};

struct PlacedHitchSelectedIdentityEvent {
    std::uint64_t presentation_frame = 0;
    std::uint64_t bridge_frame = 0;
    std::uint64_t resource_serial = 0;
    double appearance_ms = 0.0;
    std::array<std::string, 2> keys{};
    std::array<std::uint64_t, 2> resolver_generations{};
    std::uint8_t count = 0;
    bool complete = false;
};

inline void placed_hitch_account_count_time(PlacedHitchFrame& frame,
    std::int64_t count_time, std::int64_t native_tick,
    std::int64_t nested_original, std::int64_t nested_finish,
    std::int64_t unit_original, std::int64_t unit_finish,
    std::int64_t world_tick, double scale,
    const std::array<PlacedHitchDirectCall, 3>& direct_calls = {},
    const std::array<PlacedHitchNativeOwnedCallback,6>& owned_callbacks = {}) noexcept {
    frame.count_time_accounting_valid=false;
    if (count_time<0 || native_tick<0 || nested_original<0 ||
        nested_finish<0 || unit_original<0 || unit_finish<0 ||
        world_tick<0 || nested_original>unit_original ||
        nested_finish>unit_finish || native_tick>count_time ||
        nested_original>native_tick ||
        nested_finish>native_tick-nested_original) return;
    auto remaining=count_time-native_tick;
    auto unmatched_original=unit_original-nested_original;
    auto unmatched_finish=unit_finish-nested_finish;
    for (const auto& call : direct_calls) {
        if (call.original_ticks<0 || call.unit_original_ticks<0 ||
            call.unit_finish_ticks<0 || call.original_ticks>remaining ||
            call.unit_original_ticks>unmatched_original ||
            call.unit_finish_ticks>unmatched_finish ||
            call.unit_original_ticks>call.original_ticks ||
            call.unit_finish_ticks>
                call.original_ticks-call.unit_original_ticks) return;
        remaining-=call.original_ticks;
        unmatched_original-=call.unit_original_ticks;
        unmatched_finish-=call.unit_finish_ticks;
    }
    if (world_tick>remaining) return;
    remaining-=world_tick;
    if (unmatched_original>remaining) return;
    remaining-=unmatched_original;
    if (unmatched_finish>remaining) return;
    remaining-=unmatched_finish;
    const auto native_exclusive=native_tick-nested_original-nested_finish;
    std::int64_t owned_total=0;
    for (const auto& callback : owned_callbacks) {
        if (callback.exclusive_ticks<0 ||
            callback.exclusive_ticks>native_exclusive-owned_total) return;
        owned_total+=callback.exclusive_ticks;
    }
    frame.count_time_accounting_valid=true;
    frame.native_lifecycle_tick_original_cpu_ms=native_tick*scale;
    frame.native_lifecycle_tick_unit_original_cpu_ms=nested_original*scale;
    frame.native_lifecycle_tick_unit_finish_cpu_ms=nested_finish*scale;
    frame.native_lifecycle_tick_exclusive_cpu_ms=
        native_exclusive*scale;
    frame.native_lifecycle_tick_unassigned_cpu_ms=
        (native_exclusive-owned_total)*scale;
    for (std::size_t i=0;i<owned_callbacks.size();++i) {
        auto& output=frame.native_tick_owned_callbacks[i];
        output=owned_callbacks[i];
        output.exclusive_cpu_ms=output.exclusive_ticks*scale;
        output.mean_cpu_ms=output.calls ?
            output.exclusive_cpu_ms/static_cast<double>(output.calls) : 0.0;
    }
    for (std::size_t i=0; i<direct_calls.size(); ++i) {
        auto& output=frame.count_time_direct_calls[i];
        output=direct_calls[i];
        output.original_cpu_ms=output.original_ticks*scale;
        output.unit_original_cpu_ms=output.unit_original_ticks*scale;
        output.unit_finish_cpu_ms=output.unit_finish_ticks*scale;
        output.exclusive_cpu_ms=(output.original_ticks-
            output.unit_original_ticks-output.unit_finish_ticks)*scale;
    }
    frame.count_time_outside_admitted_ticks_cpu_ms=remaining*scale;
}

inline bool placed_hitch_finish_partition_valid(
    const PlacedHitchFrame& frame, double tolerance = 1.0e-9) noexcept {
    const auto valid_partition = [tolerance](
        double total, double appearance, double selected_clip,
        double outer_unit_values, double remainder) noexcept {
        if (!std::isfinite(total) || !std::isfinite(appearance) ||
            !std::isfinite(selected_clip) ||
            !std::isfinite(outer_unit_values) ||
            !std::isfinite(remainder) || total < 0.0 || appearance < 0.0 ||
            selected_clip < 0.0 || outer_unit_values < 0.0 ||
            remainder < 0.0)
            return false;
        const double sum = appearance + selected_clip +
            outer_unit_values + remainder;
        const double allowed = (std::max)(
            tolerance, std::abs(total) * tolerance);
        return std::isfinite(sum) && std::abs(sum - total) <= allowed;
    };
    return valid_partition(
               frame.visual_unit_update_finish_cpu_ms,
               frame.visual_unit_update_finish_appearance_cpu_ms,
               frame.visual_unit_update_finish_selected_clip_cpu_ms,
               frame.visual_unit_update_finish_outer_unit_values_cpu_ms,
               frame.visual_unit_update_finish_other_cpu_ms) &&
        valid_partition(
               frame.slowest_visual_unit_update_finish.finish_cpu_ms,
               frame.slowest_visual_unit_update_finish.appearance_cpu_ms,
               frame.slowest_visual_unit_update_finish.selected_clip_cpu_ms,
               frame.slowest_visual_unit_update_finish.
                   outer_unit_values_cpu_ms,
               frame.slowest_visual_unit_update_finish.other_cpu_ms);
}

inline bool placed_hitch_scene_partition_usable(
    const PlacedHitchFrame& frame) noexcept {
    return frame.bridge_fresh && frame.engine.scene_partition_valid &&
        frame.engine.scene_partition_calls != 0 &&
        frame.engine.scene_partition_calls == frame.engine.scene_3d_calls &&
        frame.engine.scene_partition_retained_calls >=
            frame.engine.scene_partition_calls;
}

struct BridgeProfileSnapshot;
void populate_placed_hitch_engine_frame(
    PlacedHitchFrame& frame, const BridgeProfileSnapshot* bridge,
    bool bridge_fresh) noexcept;

enum class PlacedHitchCaptureAction : std::uint8_t {
    arm_recorder,
    consume_while_active,
    normal_capture,
};

class PlacedHitchProfile {
public:
    static constexpr std::size_t capacity = 8192;
    static constexpr std::int64_t window_seconds = 5;
    // The harness may lengthen the next recording; finishing restores 5 s.
    void set_window_seconds(std::int64_t seconds) noexcept {
        window_seconds_ = seconds > 0 ? seconds : window_seconds;
    }
    static constexpr std::size_t frame_record_bytes = sizeof(PlacedHitchFrame);
    static constexpr std::size_t maximum_buffer_bytes =
        capacity * frame_record_bytes;

    bool arm(std::uint64_t bridge_frame = 0, bool bridge_frame_valid = false,
             std::uint64_t world_epoch = 0) {
        if (active_ || (finished_ && !export_taken_)) return false;
        if (frames_.capacity() < capacity) frames_.reserve(capacity);
        frames_.clear();
        selected_identity_events_ = {};
        selected_identity_event_count_ = 0;
        selected_identity_event_overflow_ = false;
        frequency_ = anchor_ = previous_ = deadline_ = 0;
        elapsed_ms_ = 0.0;
        reason_ = PlacedHitchFinishReason::none;
        finished_ = anchored_ = export_taken_ = false;
        discard_next_interval_ = true;
        warmup_interval_excluded_ = false;
        last_bridge_frame_ = bridge_frame;
        has_last_bridge_frame_ = bridge_frame_valid && bridge_frame != 0;
        anchor_world_epoch_ = world_epoch;
        ++sequence_id_;
        active_ = true;
        return true;
    }

    bool active() const noexcept { return active_; }
    bool finished() const noexcept { return finished_; }
    bool anchored() const noexcept { return anchored_; }
    bool truncated() const noexcept {
        return reason_ == PlacedHitchFinishReason::truncated;
    }
    bool invalid() const noexcept {
        return finished_ && reason_ != PlacedHitchFinishReason::complete &&
               reason_ != PlacedHitchFinishReason::truncated;
    }
    PlacedHitchFinishReason finish_reason() const noexcept { return reason_; }
    const std::vector<PlacedHitchFrame>& frames() const noexcept { return frames_; }
    const std::array<PlacedHitchSelectedIdentityEvent, 2>& selected_identity_events() const noexcept {
        return selected_identity_events_;
    }
    std::uint8_t selected_identity_event_count() const noexcept {
        return selected_identity_event_count_;
    }
    bool selected_identity_event_overflow() const noexcept {
        return selected_identity_event_overflow_;
    }
    void observe_selected_identity(
        std::uint64_t presentation_frame, std::uint64_t bridge_frame,
        std::uint64_t resource_serial, double appearance_ms,
        const std::array<std::array<char, 256>, 2>& keys,
        const std::array<std::uint64_t, 2>& generations,
        std::uint8_t count, bool complete) noexcept {
        if (!count) return;
        if (count > 2) { selected_identity_event_overflow_ = true; return; }
        std::size_t index = selected_identity_event_count_;
        if (index == selected_identity_events_.size()) {
            selected_identity_event_overflow_ = true;
            index = selected_identity_events_[0].appearance_ms <=
                selected_identity_events_[1].appearance_ms ? 0 : 1;
            if (appearance_ms <= selected_identity_events_[index].appearance_ms) return;
        } else ++selected_identity_event_count_;
        try {
            auto& event = selected_identity_events_[index];
            event.presentation_frame = presentation_frame;
            event.bridge_frame = bridge_frame;
            event.resource_serial = resource_serial;
            event.appearance_ms = appearance_ms;
            for (std::size_t model = 0; model < count; ++model)
                event.keys[model] = keys[model].data();
            event.resolver_generations = generations;
            event.count = count;
            event.complete = complete;
        } catch (...) {
            selected_identity_event_overflow_ = true;
            selected_identity_events_[index].complete = false;
        }
    }
    double elapsed_ms() const noexcept { return elapsed_ms_; }
    std::uint64_t sequence_id() const noexcept { return sequence_id_; }
    bool warmup_interval_excluded() const noexcept {
        return warmup_interval_excluded_;
    }

    bool observe_world_epoch(std::uint64_t world_epoch,
                             bool bridge_fresh) noexcept {
        if (!active_ || finished_ || !bridge_fresh || !world_epoch ||
            !anchor_world_epoch_ || world_epoch == anchor_world_epoch_)
            return false;
        return finish(PlacedHitchFinishReason::world_transition);
    }

    bool observe_bridge_frame(std::uint64_t bridge_frame) noexcept {
        const bool fresh = bridge_frame != 0 &&
            ((!has_last_bridge_frame_ && bridge_frame == 1) ||
             (has_last_bridge_frame_ &&
              last_bridge_frame_ != (std::numeric_limits<std::uint64_t>::max)() &&
              bridge_frame == last_bridge_frame_ + 1));
        if (bridge_frame != 0) {
            last_bridge_frame_ = bridge_frame;
            has_last_bridge_frame_ = true;
        }
        return fresh;
    }

    bool anchor_present(std::int64_t now, std::int64_t frequency) noexcept {
        if (!active_ || finished_) return false;
        if (!valid_clock(now, frequency) || anchored_)
            return finish(PlacedHitchFinishReason::invalid_clock);
        if (frequency > ((std::numeric_limits<std::int64_t>::max)() - now) /
                            window_seconds_)
            return finish(PlacedHitchFinishReason::invalid_clock);
        frequency_ = frequency;
        anchor_ = previous_ = now;
        deadline_ = now + frequency * window_seconds_;
        anchored_ = true;
        return false;
    }

    bool observe_present(std::int64_t now, std::int64_t frequency,
                         PlacedHitchFrame frame) noexcept {
        if (!active_ || finished_) return false;
        if (!anchored_ || !valid_clock(now, frequency) || frequency != frequency_)
            return finish(PlacedHitchFinishReason::invalid_clock);
        if (now <= previous_ || now - previous_ > frequency_)
            return finish(PlacedHitchFinishReason::clock_discontinuity);
        if (!frame.focused)
            return finish(PlacedHitchFinishReason::focus_lost);
        if (!frame.present_succeeded)
            return finish(PlacedHitchFinishReason::present_failed);
        if (!frame.previous_trim_succeeded)
            return finish(PlacedHitchFinishReason::trim_failed);

        frame.interval_ms = static_cast<double>(now - previous_) * 1000.0 /
                            static_cast<double>(frequency_);
        if (frame.bridge_fresh && frame.outer_timing_fresh) {
            frame.partition_measured_ms =
                frame.previous_post_present_trim_ms +
                frame.message_handling_ms + frame.count_time_after_clock_ms +
                frame.interface_on_idle_ms + frame.engine.frame_cpu_ms +
                frame.post_end_frame_to_present_entry_ms +
                frame.renderer_present_entry_to_present_return_ms;
            frame.partition_residual_ms = (std::max)(
                0.0, frame.interval_ms - frame.partition_measured_ms);
        }
        previous_ = now;
        if (discard_next_interval_) {
            discard_next_interval_ = false;
            warmup_interval_excluded_ = true;
            elapsed_ms_ = elapsed(now);
            return false;
        }
        if (frames_.size() == capacity) {
            elapsed_ms_ = elapsed(now);
            return finish(PlacedHitchFinishReason::truncated);
        }
        frames_.push_back(std::move(frame));
        elapsed_ms_ = elapsed(now);
        if (now >= deadline_)
            return finish(PlacedHitchFinishReason::complete);
        return false;
    }

    bool finish_invalid(PlacedHitchFinishReason reason) noexcept {
        if (!active_ || finished_) return false;
        if (reason == PlacedHitchFinishReason::none ||
            reason == PlacedHitchFinishReason::complete ||
            reason == PlacedHitchFinishReason::truncated)
            reason = PlacedHitchFinishReason::invalid_clock;
        if (anchored_ && previous_ >= anchor_ && frequency_ > 0)
            elapsed_ms_ = elapsed(previous_);
        return finish(reason);
    }

    bool take_export_request() noexcept {
        if (!finished_ || export_taken_) return false;
        export_taken_ = true;
        return true;
    }

private:
    static bool valid_clock(std::int64_t now, std::int64_t frequency) noexcept {
        return now > 0 && frequency > 0;
    }
    double elapsed(std::int64_t now) const noexcept {
        return static_cast<double>(now - anchor_) * 1000.0 /
               static_cast<double>(frequency_);
    }
    bool finish(PlacedHitchFinishReason reason) noexcept {
        window_seconds_ = window_seconds;
        active_ = false;
        finished_ = true;
        reason_ = reason;
        return true;
    }

    std::vector<PlacedHitchFrame> frames_;
    std::array<PlacedHitchSelectedIdentityEvent, 2> selected_identity_events_{};
    std::uint8_t selected_identity_event_count_ = 0;
    bool selected_identity_event_overflow_ = false;
    std::int64_t frequency_ = 0;
    std::int64_t anchor_ = 0;
    std::int64_t previous_ = 0;
    std::int64_t deadline_ = 0;
    std::int64_t window_seconds_ = window_seconds;
    std::uint64_t last_bridge_frame_ = 0;
    std::uint64_t anchor_world_epoch_ = 0;
    std::uint64_t sequence_id_ = 0;
    double elapsed_ms_ = 0.0;
    PlacedHitchFinishReason reason_ = PlacedHitchFinishReason::none;
    bool active_ = false;
    bool finished_ = false;
    bool anchored_ = false;
    bool has_last_bridge_frame_ = false;
    bool export_taken_ = false;
    bool discard_next_interval_ = true;
    bool warmup_interval_excluded_ = false;
};

inline PlacedHitchCaptureAction placed_hitch_capture_action(
    bool enabled, const PlacedHitchProfile& profile) noexcept {
    if (!enabled)
        return PlacedHitchCaptureAction::normal_capture;
    return profile.active() ? PlacedHitchCaptureAction::consume_while_active
                            : PlacedHitchCaptureAction::arm_recorder;
}

inline bool placed_hitch_post_complete_capture_allowed(
    const PlacedHitchProfile& profile, bool export_saved) noexcept {
    return export_saved && profile.finished() &&
        profile.finish_reason() == PlacedHitchFinishReason::complete;
}

inline std::string placed_hitch_profile_file_name(std::uint64_t sequence_id) {
    std::ostringstream name;
    name.imbue(std::locale::classic());
    name << "outer_frame_gap_profile-" << std::setfill('0') << std::setw(4)
         << sequence_id << ".json";
    return name.str();
}

enum class PlacedHitchExportStatus : std::uint8_t {
    saved,
    debug_directory_missing,
    path_too_long,
    open_failed,
    write_failed,
    close_failed,
    allocation_failed,
    serialization_failed,
};

inline const char* placed_hitch_export_status_name(
    PlacedHitchExportStatus status) noexcept {
    switch (status) {
    case PlacedHitchExportStatus::saved: return "saved";
    case PlacedHitchExportStatus::debug_directory_missing:
        return "debug_directory_missing";
    case PlacedHitchExportStatus::path_too_long: return "path_too_long";
    case PlacedHitchExportStatus::open_failed: return "open_failed";
    case PlacedHitchExportStatus::write_failed: return "write_failed";
    case PlacedHitchExportStatus::close_failed: return "close_failed";
    case PlacedHitchExportStatus::allocation_failed:
        return "allocation_failed";
    case PlacedHitchExportStatus::serialization_failed:
        return "serialization_failed";
    }
    return "unknown";
}

struct PlacedHitchExportResult {
    PlacedHitchExportStatus status =
        PlacedHitchExportStatus::serialization_failed;
    std::string path;
    std::size_t expected_bytes = 0;
    std::size_t written_bytes = 0;
    int error_code = 0;

    bool saved() const noexcept {
        return status == PlacedHitchExportStatus::saved;
    }
};

#ifdef EID3D11_TESTING
enum class PlacedHitchExportTestFailure : std::uint8_t {
    none,
    open,
    write,
    close,
};
#endif

inline PlacedHitchExportResult write_placed_hitch_profile_file(
    const std::string& root, std::uint64_t sequence_id,
    const std::string& output
#ifdef EID3D11_TESTING
    , PlacedHitchExportTestFailure injected_failure =
          PlacedHitchExportTestFailure::none
#endif
    ) noexcept {
    PlacedHitchExportResult result{};
    result.expected_bytes = output.size();
    try {
        if (root.empty()) {
            result.status = PlacedHitchExportStatus::debug_directory_missing;
            result.error_code = ENOENT;
            return result;
        }
        result.path = root + "\\" +
            placed_hitch_profile_file_name(sequence_id);
        // The renderer and its environment lookup both use the Win32 MAX_PATH
        // contract. Leave room for the trailing NUL before opening the file.
        if (result.path.size() >= 260u) {
            result.status = PlacedHitchExportStatus::path_too_long;
            result.error_code = ENAMETOOLONG;
            return result;
        }

#ifdef EID3D11_TESTING
        if (injected_failure == PlacedHitchExportTestFailure::open) {
            result.status = PlacedHitchExportStatus::open_failed;
            result.error_code = EACCES;
            return result;
        }
#endif

        FILE* file = nullptr;
        // Exclusive creation preserves any prior recording with the same
        // process-local sequence name; only this successful open authorizes
        // cleanup of a partial file below.
        const errno_t open_error = fopen_s(&file, result.path.c_str(), "wbx");
        if (open_error != 0 || !file) {
            result.status = PlacedHitchExportStatus::open_failed;
            result.error_code = open_error != 0 ? static_cast<int>(open_error)
                                                : EIO;
            return result;
        }

        std::size_t requested_bytes = output.size();
#ifdef EID3D11_TESTING
        if (injected_failure == PlacedHitchExportTestFailure::write &&
            requested_bytes != 0)
            --requested_bytes;
#endif
        errno = 0;
        result.written_bytes = std::fwrite(
            output.data(), 1, requested_bytes, file);
        const bool complete_write =
            result.written_bytes == result.expected_bytes;
        int write_error = complete_write ? 0 : (errno != 0 ? errno : EIO);
        if (complete_write) errno = 0;
        const int flush_error = std::fflush(file);
        if (flush_error != 0 && write_error == 0)
            write_error = errno != 0 ? errno : EIO;
#ifdef EID3D11_TESTING
        const bool injected_write_failure =
            injected_failure == PlacedHitchExportTestFailure::write;
#else
        constexpr bool injected_write_failure = false;
#endif
        if (!complete_write || flush_error != 0 || injected_write_failure) {
            result.status = PlacedHitchExportStatus::write_failed;
            result.error_code = write_error != 0 ? write_error : EIO;
            (void)std::fclose(file);
            (void)std::remove(result.path.c_str());
            return result;
        }

        int close_error = std::fclose(file);
        int close_error_code = close_error != 0 ? (errno != 0 ? errno : EIO) : 0;
#ifdef EID3D11_TESTING
        if (injected_failure == PlacedHitchExportTestFailure::close &&
            close_error == 0) {
            close_error = EOF;
            close_error_code = EIO;
        }
#endif
        if (close_error != 0) {
            result.status = PlacedHitchExportStatus::close_failed;
            result.error_code = close_error_code;
            (void)std::remove(result.path.c_str());
            return result;
        }

        result.status = PlacedHitchExportStatus::saved;
        result.error_code = 0;
        return result;
    } catch (...) {
        result.status = PlacedHitchExportStatus::allocation_failed;
        result.path.clear();
        result.error_code = ENOMEM;
        return result;
    }
}

namespace placed_hitch_detail {

inline double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size())));
    return values[(std::max)(std::size_t{1}, rank) - 1];
}

inline std::size_t tail_count(std::size_t frames, double fraction) noexcept {
    if (!frames) return 0;
    return (std::max)(std::size_t{1}, static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(frames))));
}

inline void append_json_text(std::ostream& out, const char* value) {
    if (!value || !*value) {
        out << "null";
        return;
    }
    static constexpr char hexadecimal[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(value);
         *cursor; ++cursor) {
        switch (*cursor) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (*cursor < 0x20) {
                out << "\\u00" << hexadecimal[*cursor >> 4]
                    << hexadecimal[*cursor & 0x0f];
            } else {
                out << static_cast<char>(*cursor);
            }
            break;
        }
    }
    out << '"';
}

} // namespace placed_hitch_detail

inline std::string format_placed_hitch_profile_json(
    const PlacedHitchProfile& profile) {
    std::vector<double> intervals;
    intervals.reserve(profile.frames().size());
    double maximum = 0.0;
    for (const auto& frame : profile.frames()) {
        intervals.push_back(frame.interval_ms);
        maximum = (std::max)(maximum, frame.interval_ms);
    }

    const char* status = profile.truncated() ? "truncated" :
                         profile.invalid() ? "invalid" : "complete";
    std::ostringstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6);
    out << "{\"schema\":12,\"sequence_id\":" << profile.sequence_id()
        << ",\"status\":\"" << status
        << "\",\"finish_reason\":\""
        << placed_hitch_finish_reason_name(profile.finish_reason())
        << "\",\"duration_ms\":" << profile.elapsed_ms()
        << ",\"frame_count\":" << profile.frames().size()
        << ",\"first_complete_interval_excluded\":"
        << (profile.warmup_interval_excluded() ? "true" : "false")
        << ",\"buffer_capacity_frames\":" << PlacedHitchProfile::capacity
        << ",\"frame_record_bytes\":" << PlacedHitchProfile::frame_record_bytes
        << ",\"buffer_capacity_bytes\":" << PlacedHitchProfile::maximum_buffer_bytes
        << ",\"tail_sample_counts\":{\"one_percent\":"
        << placed_hitch_detail::tail_count(profile.frames().size(), 0.01)
        << ",\"point_one_percent\":"
        << placed_hitch_detail::tail_count(profile.frames().size(), 0.001)
        << "},\"interval_boundary\":\"successful_present_return_to_successful_present_return\""
        << ",\"interval_includes_previous_post_present_trim\":true"
        << ",\"renderer_present_cpu_boundary\":\"Renderer_present_entry_to_swap_Present_return\""
        << ",\"count_time_boundary\":\"active_timeGetTime_call_entry_to_InterfaceOnIdle_call_entry_partial_CountTime_after_clock\""
        << ",\"visual_unit_update_subscopes\":\"exclusive_inside_CountTime_original_and_finish_only_wrapper_prefix_and_failure_bookkeeping_unassigned_not_added_to_partition_measured\""
        << ",\"visual_unit_update_finish_operations\":\"appearance_selected_clip_and_outer_unit_values_are_exclusive_other_is_exact_finish_remainder_slowest_value_copied_before_scope_release\""
        << ",\"native_depth_selection_partition\":\"selected_equals_warm_plus_cold_first_cold_reasons_sum_to_cold_registration_callbacks_equal_cold_for_complete_frames\""
        << ",\"shadow_decision_scope\":\"latest_begin_cause_masks_first_numeric_difference_only_dirty_and_source_counts_cover_frame_validity_not_assessed_unless_compared_per_slice_selection_counts_overlap\""
        << ",\"post_engine_boundary\":\"bridge_EndFrame_return_to_Renderer_present_entry_Draw2D_cursor_proxy_envelope\""
        << ",\"engine_fields_are_hierarchical\":true"
        << ",\"scene_partition\":\"six_exclusive_scene_cpu_leaves_sum_to_scene_3d_cpu_ms_when_valid\""
        << ",\"scene_partition_flush_scope\":\"queue_native_flush_and_queue_ui_flush_fields_cover_all_matching_calls_inside_scene_including_calls_outside_Device_queueDraw\""
        << ",\"gpu_snapshot_scope\":\"latest_nonblocking_resolved_profiler_frame_id_not_joined_to_presentation_frame\""
        << ",\"partition_residual\":\"computed_only_when_bridge_and_outer_timing_are_fresh_otherwise_zero_unassigned\""
        << ",\"interval_summary_ms\":{\"median\":"
        << placed_hitch_detail::percentile(intervals, 0.5)
        << ",\"p95\":" << placed_hitch_detail::percentile(intervals, 0.95)
        << ",\"max\":" << maximum << "},\"selected_identity_event_overflow\":"
        << (profile.selected_identity_event_overflow() ? "true" : "false")
        << ",\"selected_identity_events\":[";
    for (std::size_t index = 0; index < profile.selected_identity_event_count(); ++index) {
        if (index) out << ',';
        const auto& event = profile.selected_identity_events()[index];
        out << "{\"presentation_frame\":" << event.presentation_frame
            << ",\"bridge_frame\":" << event.bridge_frame
            << ",\"resource_serial\":" << event.resource_serial
            << ",\"appearance_ms\":" << event.appearance_ms
            << ",\"complete\":" << (event.complete ? "true" : "false")
            << ",\"models\":[";
        for (std::size_t model = 0; model < event.count; ++model) {
            if (model) out << ',';
            out << "{\"resolver_generation\":" << event.resolver_generations[model]
                << ",\"identity\":";
            placed_hitch_detail::append_json_text(out, event.keys[model].c_str());
            out << '}';
        }
        out << "]}";
    }
    out << "],\"frames\":[";

    bool first = true;
    for (const auto& frame : profile.frames()) {
        if (!first) out << ',';
        first = false;
        const auto& engine = frame.engine;
        const auto& renderer = frame.renderer;
        const auto& color = renderer.color_resource;
        const bool color_partition_valid = placed_hitch_color_partition_valid(color);
        const auto color_scale = renderer.color_tick_to_ms;
        const auto texture_other = color_partition_valid ?
            color.texture_ticks - color.texture_create_ticks - color.texture_flush_ticks : 0;
        const auto mesh_upload = color_partition_valid ?
            color.geometry_upload_ticks - color.nonmesh_geometry_upload_ticks : 0;
        const auto mesh_arena = color_partition_valid ?
            color.arena_ticks - color.nonmesh_arena_ticks : 0;
        const auto nonmesh_resource = color_partition_valid ?
            color.nonmesh_geometry_upload_ticks + color.nonmesh_arena_ticks : 0;
        const auto mesh_other = color_partition_valid ?
            color.mesh_ticks - mesh_upload - mesh_arena - color.mesh_flush_ticks : 0;
        const auto outside_flush = color_partition_valid ?
            color.flush_ticks - color.mesh_flush_ticks - color.texture_flush_ticks : 0;
        const auto record_other = color_partition_valid ?
            color.total_ticks - color.texture_ticks - color.mesh_ticks -
                nonmesh_resource - outside_flush : 0;
        const auto cold_outside_flush = color_partition_valid ?
            color.cold_flush_ticks - color.mesh_flush_ticks - color.texture_flush_ticks : 0;
        const auto cold_record_other = color_partition_valid ?
            color.cold_registration_ticks - color.texture_ticks - color.mesh_ticks -
                nonmesh_resource - cold_outside_flush : 0;
        out << "{\"presentation_frame\":" << frame.presentation_frame
            << ",\"bridge_frame\":" << frame.bridge_frame
            << ",\"world_epoch\":" << frame.world_epoch
            << ",\"focused\":" << (frame.focused ? "true" : "false")
            << ",\"bridge_fresh\":" << (frame.bridge_fresh ? "true" : "false")
            << ",\"outer_timing_fresh\":"
            << (frame.outer_timing_fresh ? "true" : "false")
            << ",\"present_succeeded\":"
            << (frame.present_succeeded ? "true" : "false")
            << ",\"previous_trim_succeeded\":"
            << (frame.previous_trim_succeeded ? "true" : "false")
            << ",\"interval_ms\":" << frame.interval_ms
            << ",\"renderer_present_entry_to_present_return_ms\":"
            << frame.renderer_present_entry_to_present_return_ms
            << ",\"outer_loop\":{\"count_time_after_clock_ms\":"
            << frame.count_time_after_clock_ms
            << ",\"interface_on_idle_ms\":" << frame.interface_on_idle_ms
            << ",\"message_handling_ms\":" << frame.message_handling_ms
            << ",\"post_end_frame_to_present_entry_ms\":"
            << frame.post_end_frame_to_present_entry_ms
            << ",\"previous_post_present_trim_ms\":"
            << frame.previous_post_present_trim_ms
            << ",\"visual_unit_update_original_cpu_ms\":"
            << frame.visual_unit_update_original_cpu_ms
            << ",\"visual_unit_update_finish_cpu_ms\":"
            << frame.visual_unit_update_finish_cpu_ms
            << ",\"visual_unit_update_finish_appearance_cpu_ms\":"
            << frame.visual_unit_update_finish_appearance_cpu_ms
            << ",\"visual_unit_update_finish_selected_clip_cpu_ms\":"
            << frame.visual_unit_update_finish_selected_clip_cpu_ms
            << ",\"visual_unit_update_finish_outer_unit_values_cpu_ms\":"
            << frame.visual_unit_update_finish_outer_unit_values_cpu_ms
            << ",\"visual_unit_update_finish_other_cpu_ms\":"
            << frame.visual_unit_update_finish_other_cpu_ms
            << ",\"native_lifecycle_tick_original_cpu_ms\":"
            << frame.native_lifecycle_tick_original_cpu_ms
            << ",\"native_lifecycle_tick_unit_original_cpu_ms\":"
            << frame.native_lifecycle_tick_unit_original_cpu_ms
            << ",\"native_lifecycle_tick_unit_finish_cpu_ms\":"
            << frame.native_lifecycle_tick_unit_finish_cpu_ms
            << ",\"native_lifecycle_tick_exclusive_cpu_ms\":"
            << frame.native_lifecycle_tick_exclusive_cpu_ms
            << ",\"native_lifecycle_tick_unassigned_cpu_ms\":"
            << frame.native_lifecycle_tick_unassigned_cpu_ms
            << ",\"native_lifecycle_tick_owned_callbacks\":{";
        constexpr const char* native_owned_names[] = {
            "packet_open", "database_binding", "selected_clip",
            "received_packet_completion", "unit_segment_post", "appearance_outside_finish"};
        for (std::size_t i=0;i<frame.native_tick_owned_callbacks.size();++i) {
            const auto& callback=frame.native_tick_owned_callbacks[i];
            if (i) out << ',';
            out << '"' << native_owned_names[i] << "\":{\"exclusive_cpu_ms\":"
                << callback.exclusive_cpu_ms
                << ",\"mean_cpu_ms\":" << callback.mean_cpu_ms
                << ",\"calls\":" << callback.calls << '}';
        }
        out << '}'
            << ",\"visual_events\":{\"complete\":"
            << (frame.visual_events_complete ? "true" : "false")
            << ",\"overflow\":" << frame.visual_event_overflow
            << ",\"items\":[";
        constexpr const char* visual_event_kinds[] = {
            "resource_begin", "resource_finish", "resource_destroy", "appearance"};
        constexpr const char* appearance_callbacks[] = {
            "unknown", "virtual_create", "load_template", "load_figures",
            "addon_load_figures", "change_weapon", "set_materials", "load_figure",
            "add_unloaded", "final_data", "load_textures", "change_texture",
            "highlight", "plan_add_figure", "plan_remove_figure", "complection",
            "database_binding", "packet_finish"};
        constexpr const char* visual_event_phases[] = {
            "outside_unit_update", "unit_original", "unit_finish"};
        for (std::size_t i=0;i<(std::min)(std::size_t(frame.visual_event_count),
                frame.visual_events.size());++i) {
            const auto& event=frame.visual_events[i];
            if (i) out << ',';
            out << "{\"sequence\":" << event.sequence
                << ",\"kind\":\"" << (event.kind<std::size(visual_event_kinds) ?
                    visual_event_kinds[event.kind] : "unknown")
                << "\",\"phase\":\"" << (event.phase<std::size(visual_event_phases) ?
                    visual_event_phases[event.phase] : "unknown")
                << "\",\"callback\":\"" << (event.callback<std::size(appearance_callbacks) ?
                    appearance_callbacks[event.callback] : "unknown")
                << "\",\"source_mask\":" << event.source_mask
                << ",\"contributors\":[";
            bool first_contributor=true;
            for (std::size_t source=0;source<std::size(appearance_callbacks);++source)
                if (event.source_mask & (1u << source)) {
                    if (!first_contributor) out << ',';
                    first_contributor=false;
                    out << '"' << appearance_callbacks[source] << '"';
                }
            out << "]"
                << ",\"signed_id\":" << event.signed_id
                << ",\"serial\":" << event.serial
                << ",\"exclusive_cpu_ms\":" << event.exclusive_cpu_ms
                << ",\"change_mask\":" << event.change_mask
                << ",\"channels\":{\"changed\":" << ((event.change_mask&1u) ? "true" : "false")
                << ",\"materials\":" << ((event.change_mask&2u) ? "true" : "false")
                << ",\"textures\":" << ((event.change_mask&4u) ? "true" : "false")
                << ",\"completed_textures\":" << ((event.change_mask&8u) ? "true" : "false")
                << ",\"emissive\":" << ((event.change_mask&16u) ? "true" : "false")
                << ",\"specular\":" << ((event.change_mask&32u) ? "true" : "false")
                << ",\"replace_weapons\":" << ((event.change_mask&64u) ? "true" : "false")
                << ",\"rebound_parts\":" << ((event.change_mask&128u) ? "true" : "false")
                << '}'
                << ",\"assembly_before\":" << (event.assembly_before ? "true" : "false")
                << ",\"assembly_after\":" << (event.assembly_after ? "true" : "false")
                << ",\"succeeded\":" << (event.succeeded ? "true" : "false")
                << '}';
        }
        out << "]}"
            << ",\"direct_calls\":{";
        constexpr const char* direct_names[] = {
            "pre_tick", "fixed_step", "post_tick"};
        for (std::size_t i=0; i<frame.count_time_direct_calls.size(); ++i) {
            const auto& call=frame.count_time_direct_calls[i];
            if (i) out << ',';
            out << '"' << direct_names[i] << "\":{\"original_cpu_ms\":"
                << call.original_cpu_ms
                << ",\"unit_original_cpu_ms\":"
                << call.unit_original_cpu_ms
                << ",\"unit_finish_cpu_ms\":"
                << call.unit_finish_cpu_ms
                << ",\"exclusive_cpu_ms\":" << call.exclusive_cpu_ms
                << ",\"calls\":" << call.calls << '}';
        }
        out << '}'
            << ",\"count_time_outside_admitted_ticks_cpu_ms\":"
            << frame.count_time_outside_admitted_ticks_cpu_ms
            << ",\"count_time_accounting_valid\":"
            << (frame.count_time_accounting_valid ? "true" : "false")
            << ",\"visual_world_tick_cpu_ms\":"
            << frame.visual_world_tick_cpu_ms
            << ",\"visual_world_tick_objects_cpu_ms\":"
            << frame.visual_world_tick_objects_cpu_ms
            << ",\"visual_world_tick_terrain_cpu_ms\":"
            << frame.visual_world_tick_terrain_cpu_ms
            << ",\"visual_world_tick_bridge_forced_cpu_ms\":"
            << frame.visual_world_tick_bridge_forced_cpu_ms
            << ",\"visual_world_tick_commit_cpu_ms\":"
            << frame.visual_world_tick_commit_cpu_ms
            << ",\"post_engine_surface_proxy_cpu_ms\":"
            << frame.post_engine_surface_proxy_cpu_ms
            << ",\"post_engine_surface_flush_cpu_ms\":"
            << frame.post_engine_surface_flush_cpu_ms
            << ",\"post_engine_surface_cpu_rect_cpu_ms\":"
            << frame.post_engine_surface_cpu_rect_cpu_ms
            << ",\"post_engine_completed_surface_cpu_ms\":"
            << frame.post_engine_completed_surface_cpu_ms
            << ",\"partition_measured_ms\":" << frame.partition_measured_ms
            << ",\"partition_residual_ms\":" << frame.partition_residual_ms
            << ",\"count_time_calls\":" << frame.count_time_calls
            << ",\"interface_on_idle_calls\":"
            << frame.interface_on_idle_calls
            << ",\"message_handler_calls\":"
            << frame.message_handler_calls
            << ",\"visual_unit_update_original_calls\":"
            << frame.visual_unit_update_original_calls
            << ",\"visual_unit_update_finish_calls\":"
            << frame.visual_unit_update_finish_calls
            << ",\"visual_unit_update_finish_appearance_calls\":"
            << frame.visual_unit_update_finish_appearance_calls
            << ",\"visual_unit_update_finish_selected_clip_calls\":"
            << frame.visual_unit_update_finish_selected_clip_calls
            << ",\"visual_unit_update_finish_outer_unit_values_calls\":"
            << frame.visual_unit_update_finish_outer_unit_values_calls
            << ",\"native_lifecycle_tick_original_calls\":"
            << frame.native_lifecycle_tick_original_calls
            << ",\"visual_world_tick_calls\":"
            << frame.visual_world_tick_calls
            << ",\"visual_world_tick_manager_objects\":"
            << frame.visual_world_tick_manager_objects
            << ",\"visual_world_tick_terrain_layers\":"
            << frame.visual_world_tick_terrain_layers
            << ",\"visual_world_tick_terrain_noise_values\":"
            << frame.visual_world_tick_terrain_noise_values
            << ",\"visual_world_tick_bridge_planes\":"
            << frame.visual_world_tick_bridge_planes
            << ",\"visual_world_tick_forced_ids\":"
            << frame.visual_world_tick_forced_ids
            << ",\"visual_world_tick_committed_entities\":"
            << frame.visual_world_tick_committed_entities
            << ",\"post_engine_surface_proxy_calls\":"
            << frame.post_engine_surface_proxy_calls
            << ",\"post_engine_surface_flush_calls\":"
            << frame.post_engine_surface_flush_calls
            << ",\"post_engine_surface_cpu_rect_calls\":"
            << frame.post_engine_surface_cpu_rect_calls
            << ",\"post_engine_surface_cpu_rect_pixels\":"
            << frame.post_engine_surface_cpu_rect_pixels
            << ",\"post_engine_completed_surface_calls\":"
            << frame.post_engine_completed_surface_calls
            << ",\"post_engine_completed_surface_bytes\":"
            << frame.post_engine_completed_surface_bytes
            << ",\"post_engine_surface_material_group_full_scans\":"
            << frame.post_engine_surface_material_group_full_scans
            << ",\"slowest_visual_unit_update_finish\":{\"cpu_ms\":"
            << frame.slowest_visual_unit_update_finish.finish_cpu_ms
            << ",\"appearance_cpu_ms\":"
            << frame.slowest_visual_unit_update_finish.appearance_cpu_ms
            << ",\"selected_clip_cpu_ms\":"
            << frame.slowest_visual_unit_update_finish.selected_clip_cpu_ms
            << ",\"outer_unit_values_cpu_ms\":"
            << frame.slowest_visual_unit_update_finish.outer_unit_values_cpu_ms
            << ",\"other_cpu_ms\":"
            << frame.slowest_visual_unit_update_finish.other_cpu_ms
            << ",\"resource\":{\"available\":"
            << (frame.slowest_visual_unit_update_finish.has_scope ?
                    "true" : "false")
            << ",\"signed_id\":"
            << frame.slowest_visual_unit_update_finish.signed_id
            << ",\"serial\":"
            << frame.slowest_visual_unit_update_finish.resource_serial
            << "},\"result\":\""
            << (!frame.slowest_visual_unit_update_finish.has_scope ?
                    "no_scope" :
                frame.slowest_visual_unit_update_finish.completion_succeeded ?
                    "success" : "failed")
            << "\",\"failure_status_code\":"
            << frame.slowest_visual_unit_update_finish.failure_status
            << ",\"failure_status\":";
        placed_hitch_detail::append_json_text(
            out,
            frame.slowest_visual_unit_update_finish.failure_status_name.data());
        out << ",\"failure_reason\":";
        placed_hitch_detail::append_json_text(
            out,
            frame.slowest_visual_unit_update_finish.failure_reason.data());
        out << ",\"appearance\":{\"attempted\":"
            << (frame.slowest_visual_unit_update_finish.appearance_attempted ?
                    "true" : "false")
            << ",\"changed\":"
            << (frame.slowest_visual_unit_update_finish.appearance_changed ?
                    "true" : "false")
            << ",\"missing_assembly\":"
            << (frame.slowest_visual_unit_update_finish.
                    appearance_missing_assembly ? "true" : "false")
            << ",\"calls\":"
            << frame.slowest_visual_unit_update_finish.appearance_calls
            << ",\"result\":\""
            << (!frame.slowest_visual_unit_update_finish.appearance_attempted ?
                    "not_attempted" :
                frame.slowest_visual_unit_update_finish.appearance_succeeded ?
                    "success" : "failed")
            << "\",\"status\":";
        if (!frame.slowest_visual_unit_update_finish.appearance_attempted)
            out << "null";
        else if (frame.slowest_visual_unit_update_finish.appearance_succeeded)
            out << "\"success\"";
        else
            placed_hitch_detail::append_json_text(
                out,
                frame.slowest_visual_unit_update_finish.failure_status_name.data());
        out << ",\"reason\":";
        placed_hitch_detail::append_json_text(
            out,
            frame.slowest_visual_unit_update_finish.appearance_reason.data());
        const auto& appearance_diagnostic =
            frame.slowest_visual_unit_update_finish.appearance_diagnostic;
        out << ",\"breakdown\":";
        if (!appearance_diagnostic.available) out << "null";
        else out << "{\"accounting_valid\":"
            << (appearance_diagnostic.accounting_valid ? "true" : "false")
            << ",\"model_native_ms\":" << appearance_diagnostic.model_native_ms
            << ",\"model_owned_ms\":" << appearance_diagnostic.model_owned_ms
            << ",\"resource_partition_valid\":"
            << (appearance_diagnostic.resource_partition_valid ? "true" : "false")
            << ",\"model_provenance_ms\":" << appearance_diagnostic.model_provenance_ms
            << ",\"model_resolve_other_ms\":" << appearance_diagnostic.model_resolve_other_ms
            << ",\"model_file_read_ms\":" << appearance_diagnostic.model_file_read_ms
            << ",\"model_comparison_ms\":" << appearance_diagnostic.model_comparison_ms
            << ",\"model_owned_other_ms\":" << appearance_diagnostic.model_owned_other_ms
            << ",\"model_archive_ms\":" << appearance_diagnostic.model_archive_ms
            << ",\"asset_lookup_ms\":" << appearance_diagnostic.asset_lookup_ms
            << ",\"asset_decode_ms\":" << appearance_diagnostic.asset_decode_ms
            << ",\"material_ms\":" << appearance_diagnostic.material_ms
            << ",\"texture_ms\":" << appearance_diagnostic.texture_ms
            << ",\"publication_ms\":" << appearance_diagnostic.publication_ms
            << ",\"remainder_ms\":" << appearance_diagnostic.remainder_ms
            << ",\"model_acquisitions\":" << appearance_diagnostic.model_acquisitions
            << ",\"model_selected_bytes\":" << appearance_diagnostic.model_selected_bytes
            << ",\"model_unique_identities\":" << appearance_diagnostic.model_unique_identities
            << ",\"model_repeated_identities\":" << appearance_diagnostic.model_repeated_identities
            << ",\"model_repeated_selected_bytes\":" << appearance_diagnostic.model_repeated_selected_bytes
            << ",\"model_file_read_bytes\":" << appearance_diagnostic.model_file_read_bytes
            << ",\"model_file_read_calls\":" << appearance_diagnostic.model_file_read_calls
            << ",\"model_repeated_file_read_bytes\":" << appearance_diagnostic.model_repeated_file_read_bytes
            << ",\"model_backing_cache_hits\":" << appearance_diagnostic.model_backing_cache_hits
            << ",\"model_comparison_span_bytes\":" << appearance_diagnostic.model_comparison_span_bytes
            << ",\"model_repeated_comparison_span_bytes\":"
            << appearance_diagnostic.model_repeated_comparison_span_bytes
            << ",\"skeleton_cache_hits\":" << appearance_diagnostic.skeleton_cache_hits
            << ",\"skeleton_cache_misses\":" << appearance_diagnostic.skeleton_cache_misses
            << ",\"figure_cache_hits\":" << appearance_diagnostic.figure_cache_hits
            << ",\"figure_cache_misses\":" << appearance_diagnostic.figure_cache_misses
            << ",\"decoded_figure_bytes\":" << appearance_diagnostic.decoded_figure_bytes
            << ",\"identity_complete\":"
            << (appearance_diagnostic.identity_complete ? "true" : "false")
            << ",\"mixed_resolver_generation\":"
            << (appearance_diagnostic.mixed_resolver_generation ? "true" : "false")
            << ",\"selected_identity_count\":"
            << static_cast<unsigned>(appearance_diagnostic.selected_identity_count)
            << ",\"selected_identity_overflow\":"
            << (appearance_diagnostic.selected_identity_overflow ? "true" : "false")
            << '}';
        out << "},\"selected_clip_calls\":"
            << frame.slowest_visual_unit_update_finish.selected_clip_calls
            << ",\"outer_unit_values_calls\":"
            << frame.slowest_visual_unit_update_finish.outer_unit_values_calls
            << "}}"
            << ",\"engine\":{\"frame_cpu_ms\":" << engine.frame_cpu_ms
            << ",\"outside_scene_interval_ms\":" << engine.outside_scene_interval_ms
            << ",\"outside_entry_cpu_ms\":" << engine.outside_entry_cpu_ms
            << ",\"world_draw_cpu_ms\":" << engine.world_draw_cpu_ms
            << ",\"world_unattributed_cpu_ms\":" << engine.world_unattributed_cpu_ms
            << ",\"scene_3d_cpu_ms\":" << engine.scene_3d_cpu_ms
            << ",\"scene_3d_unattributed_cpu_ms\":" << engine.scene_3d_unattributed_cpu_ms
            << ",\"scene_partition_retained_nonqueue_cpu_ms\":"
            << engine.scene_partition_retained_nonqueue_cpu_ms
            << ",\"city_receiver_fitting_cpu_ms\":" << engine.city_retained_owner_cpu_ms[0]
            << ",\"city_caster_values_cpu_ms\":" << engine.city_retained_owner_cpu_ms[1]
            << ",\"city_depth_registration_cpu_ms\":" << engine.city_retained_owner_cpu_ms[2]
            << ",\"city_nonunit_pose_cpu_ms\":" << engine.city_retained_owner_cpu_ms[3]
            << ",\"city_visual_frame_values_cpu_ms\":" << engine.city_retained_owner_cpu_ms[4]
            << ",\"city_shadow_input_decision_cpu_ms\":" << engine.city_retained_owner_cpu_ms[5]
            << ",\"city_domain_snapshot_cpu_ms\":" << engine.city_retained_owner_cpu_ms[6]
            << ",\"city_color_submission_cpu_ms\":" << engine.city_retained_owner_cpu_ms[7]
            << ",\"city_point_shadow_cpu_ms\":" << engine.city_retained_owner_cpu_ms[8]
            << ",\"city_terrain_base_cpu_ms\":" << engine.city_retained_owner_cpu_ms[9]
            << ",\"city_lighting_publish_cpu_ms\":" << engine.city_retained_owner_cpu_ms[10]
            << ",\"city_retained_remainder_cpu_ms\":" << engine.city_retained_owner_cpu_ms[11]
            << ",\"scene_partition_object_nonqueue_cpu_ms\":"
            << engine.scene_partition_object_nonqueue_cpu_ms
            << ",\"scene_partition_queue_native_flush_cpu_ms\":"
            << engine.scene_partition_queue_native_flush_cpu_ms
            << ",\"scene_partition_queue_ui_flush_cpu_ms\":"
            << engine.scene_partition_queue_ui_flush_cpu_ms
            << ",\"scene_partition_queue_packet_or_control_cpu_ms\":"
            << engine.scene_partition_queue_packet_or_control_cpu_ms
            << ",\"scene_partition_remaining_game_cpu_ms\":"
            << engine.scene_partition_remaining_game_cpu_ms
            << ",\"scene_partition_valid\":"
            << (engine.scene_partition_valid ? "true" : "false")
            << ",\"outside_scene_3d_cpu_ms\":" << engine.outside_scene_3d_cpu_ms
            << ",\"object_draw_cpu_ms\":" << engine.object_draw_cpu_ms
            << ",\"object_draw_unattributed_cpu_ms\":" << engine.object_draw_unattributed_cpu_ms
            << ",\"object_plan_bootstrap_cpu_ms\":" << engine.object_plan_bootstrap_cpu_ms
            << ",\"object_plan_submit_cpu_ms\":" << engine.object_plan_submit_cpu_ms
            << ",\"simple_cpu_ms\":" << engine.simple_cpu_ms
            << ",\"figure_placed_cpu_ms\":" << engine.figure_placed_cpu_ms
            << ",\"figure_lit_cpu_ms\":" << engine.figure_lit_cpu_ms
            << ",\"figure_lit_morphed_cpu_ms\":" << engine.figure_lit_morphed_cpu_ms
            << ",\"figure_lit_rigid_cpu_ms\":" << engine.figure_lit_rigid_cpu_ms
            << ",\"terrain_cpu_ms\":" << engine.terrain_cpu_ms
            << ",\"terrain_outer_cpu_ms\":" << engine.terrain_outer_cpu_ms
            << ",\"terrain_capture_cpu_ms\":" << engine.terrain_capture_cpu_ms
            << ",\"terrain_submit_cpu_ms\":" << engine.terrain_submit_cpu_ms
            << ",\"world_draw_calls\":" << engine.world_draw_calls
            << ",\"scene_3d_calls\":" << engine.scene_3d_calls
            << ",\"scene_partition_calls\":"
            << engine.scene_partition_calls
            << ",\"scene_partition_retained_calls\":"
            << engine.scene_partition_retained_calls
            << ",\"scene_partition_object_outer_calls\":"
            << engine.scene_partition_object_outer_calls
            << ",\"scene_partition_queue_calls\":"
            << engine.scene_partition_queue_calls
            << ",\"object_draw_calls\":" << engine.object_draw_calls
            << ",\"object_plan_original_calls\":" << engine.object_plan_original_calls
            << ",\"object_plan_owned_calls\":" << engine.object_plan_owned_calls
            << ",\"object_plan_submitted_figures\":" << engine.object_plan_submitted_figures
            << ",\"object_plan_early_omitted_calls\":" << engine.object_plan_early_omitted_calls
            << ",\"simple_calls\":" << engine.simple_calls
            << ",\"figure_placed_calls\":" << engine.figure_placed_calls
            << ",\"figure_lit_calls\":" << engine.figure_lit_calls
            << ",\"figure_gpu_owned_calls\":" << engine.figure_gpu_owned_calls
            << ",\"figure_cpu_fallback_calls\":" << engine.figure_cpu_fallback_calls
            << ",\"figure_gpu_submit_failures\":" << engine.figure_gpu_submit_failures
            << ",\"terrain_calls\":" << engine.terrain_calls
            << ",\"terrain_outer_calls\":" << engine.terrain_outer_calls
            << ",\"terrain_gpu_owned_calls\":" << engine.terrain_gpu_owned_calls
            << ",\"terrain_cpu_fallback_calls\":" << engine.terrain_cpu_fallback_calls
            << ",\"terrain_gpu_submit_failures\":" << engine.terrain_gpu_submit_failures
            << ",\"terrain_spatial_bounds_prepared\":" << engine.terrain_spatial_bounds_prepared
            << ",\"terrain_spatial_bounds_reused\":" << engine.terrain_spatial_bounds_reused
            << "},\"renderer\":{\"logical_draws\":" << renderer.logical_draws
            << ",\"physical_batches\":" << renderer.physical_batches
            << ",\"texture_array_attempts\":" << renderer.texture_array_attempts
            << ",\"texture_array_successes\":" << renderer.texture_array_successes
            << ",\"texture_array_unavailable\":" << renderer.texture_array_unavailable
            << ",\"texture_array_format_failures\":" << renderer.texture_array_format_failures
            << ",\"texture_array_layout_failures\":" << renderer.texture_array_layout_failures
            << ",\"texture_array_allocation_failures\":" << renderer.texture_array_allocation_failures
            << ",\"terrain_topology_uploads\":" << renderer.terrain_topology_uploads
            << ",\"terrain_topology_upload_vertices\":" << renderer.terrain_topology_upload_vertices
            << ",\"terrain_color_uploads\":" << renderer.terrain_color_uploads
            << ",\"terrain_color_upload_vertices\":" << renderer.terrain_color_upload_vertices
            << ",\"retained_color_resources\":{\"partition_valid\":"
            << (color_partition_valid ? "true" : "false")
            << ",\"tagged_calls\":" << color.tagged_calls
            << ",\"warm_parts\":" << color.warm_parts
            << ",\"cold_parts\":" << color.cold_parts
            << ",\"failed_calls\":" << color.failed_calls
            << ",\"texture_cold_calls\":" << color.texture_cold_calls
            << ",\"texture_upload_bytes\":" << color.texture_upload_bytes
            << ",\"mesh_cold_calls\":" << color.mesh_cold_calls
            << ",\"geometry_upload_bytes\":" << color.geometry_upload_bytes
            << ",\"arena_grows\":" << color.arena_grows
            << ",\"arena_copy_bytes\":" << color.arena_copy_bytes
            << ",\"total_ms\":" << color.total_ticks * color_scale
            << ",\"cold_registration_inclusive_ms\":"
            << color.cold_registration_ticks * color_scale
            << ",\"texture_prepare_ms\":" << texture_other * color_scale
            << ",\"texture_create_upload_ms\":" << color.texture_create_ticks * color_scale
            << ",\"mesh_prepare_ms\":" << mesh_other * color_scale
            << ",\"geometry_upload_ms\":" << color.geometry_upload_ticks * color_scale
            << ",\"arena_grow_copy_ms\":" << color.arena_ticks * color_scale
            << ",\"mesh_geometry_upload_ms\":" << mesh_upload * color_scale
            << ",\"mesh_arena_grow_copy_ms\":" << mesh_arena * color_scale
            << ",\"nonmesh_geometry_upload_ms\":"
            << color.nonmesh_geometry_upload_ticks * color_scale
            << ",\"nonmesh_arena_grow_copy_ms\":"
            << color.nonmesh_arena_ticks * color_scale
            << ",\"flush_excluded_ms\":" << color.flush_ticks * color_scale
            << ",\"record_prepare_ms\":" << record_other * color_scale
            << ",\"cold_record_prepare_ms\":" << cold_record_other * color_scale
            << "},\"available_gpu_snapshot\":{\"available\":"
            << (renderer.gpu_snapshot_available ? "true" : "false")
            << ",\"snapshot_frame\":" << renderer.gpu_snapshot_frame
            << ",\"cpu_ms\":" << renderer.gpu_snapshot_cpu_ms
            << ",\"gpu_ms\":" << renderer.gpu_snapshot_ms
            << ",\"gpu_valid\":" << (renderer.gpu_snapshot_valid ? "true" : "false")
            << "}"
            << ",\"native_shadows\":{\"enabled\":"
            << (renderer.native_shadows_enabled ? "true" : "false")
            << ",\"draws\":" << renderer.native_shadow_draws
            << ",\"triangles\":" << renderer.native_shadow_triangles << "}"
            << ",\"shadow_decision\":{\"begin_calls\":"
            << renderer.shadow_decision.begin_calls
            << ",\"previous_valid_mask\":"
            << unsigned(renderer.shadow_decision.previous_valid_mask)
            << ",\"previous_enabled_mask\":"
            << unsigned(renderer.shadow_decision.previous_enabled_mask)
            << ",\"current_enabled_mask\":"
            << unsigned(renderer.shadow_decision.current_enabled_mask)
            << ",\"previous_spatial_mask\":"
            << unsigned(renderer.shadow_decision.previous_spatial_mask)
            << ",\"current_spatial_mask\":"
            << unsigned(renderer.shadow_decision.current_spatial_mask)
            << ",\"numeric_compared_mask\":"
            << unsigned(renderer.shadow_decision.numeric_compared_mask)
            << ",\"numeric_changed_mask\":"
            << unsigned(renderer.shadow_decision.numeric_changed_mask)
            << ",\"light_transform_changed_mask\":"
            << unsigned(renderer.shadow_decision.light_transform_changed_mask)
            << ",\"camera_slice_changed_mask\":"
            << unsigned(renderer.shadow_decision.camera_slice_changed_mask)
            << ",\"bias_changed_mask\":"
            << unsigned(renderer.shadow_decision.bias_changed_mask)
            << ",\"receiver_bounds_changed_mask\":"
            << unsigned(renderer.shadow_decision.receiver_bounds_changed_mask)
            << ",\"validity_compared_mask\":"
            << unsigned(renderer.shadow_decision.validity_compared_mask)
            << ",\"validity_changed_mask\":"
            << unsigned(renderer.shadow_decision.validity_changed_mask)
            << ",\"validity_not_assessed_mask\":"
            << unsigned(renderer.shadow_decision.current_enabled_mask &
                static_cast<std::uint8_t>(~renderer.shadow_decision.validity_compared_mask))
            << ",\"dirty_mask\":" << unsigned(renderer.shadow_decision.dirty_mask)
            << ",\"source_dirty_mask\":"
            << unsigned(renderer.shadow_decision.source_dirty_mask)
            << ",\"source_publications\":"
            << renderer.shadow_decision.source_publications
            << ",\"source_changed_visuals\":"
            << renderer.shadow_decision.source_changed_visuals
            << ",\"source_changed_terrain\":"
            << renderer.shadow_decision.source_changed_terrain
            << ",\"source_receiver_refits\":"
            << renderer.shadow_decision.source_receiver_refits
            << ",\"source_caster_refits\":"
            << renderer.shadow_decision.source_caster_refits
            << ",\"source_map_reuses\":"
            << renderer.shadow_decision.source_map_reuses
            << ",\"value_reuses\":" << renderer.shadow_decision.value_reuses
            << ",\"frame_copies\":" << renderer.shadow_decision.frame_copies;
        const auto write_shadow_counts=[&](const char* name,
            const std::array<std::uint64_t,5>& counts) {
            out << ",\"" << name << "\":[";
            for(std::size_t slice=0;slice<5;++slice) {
                if(slice)out << ',';
                out << counts[slice];
            }
            out << ']';
        };
        write_shadow_counts("previous_validity_counts",renderer.shadow_decision.previous_validity_counts);
        write_shadow_counts("current_validity_counts",renderer.shadow_decision.current_validity_counts);
        write_shadow_counts("selected_per_slice",renderer.shadow_decision.selected_per_slice);
        out << "}"
            << ",\"native_depth_selection\":{\"selected\":"
            << renderer.native_depth_selected
            << ",\"warm\":" << renderer.native_depth_warm
            << ",\"cold\":" << renderer.native_depth_cold
            << ",\"registration_callbacks\":"
            << renderer.native_depth_registration_callbacks
            << ",\"first_cold_reason\":{\"handle_owner_identity\":"
            << renderer.native_depth_cold_handle_owner_identity
            << ",\"source_revision\":"
            << renderer.native_depth_cold_source_revision
            << ",\"inherited_context\":"
            << renderer.native_depth_cold_inherited_context
            << ",\"caster_revision\":"
            << renderer.native_depth_cold_caster_revision
            << ",\"world_allocation_generation\":"
            << renderer.native_depth_cold_world_allocation_generation
            << ",\"pose_revision_generation_allocation\":"
            << renderer.native_depth_cold_pose_revision_generation_allocation
            << ",\"animation_generation_ownership\":"
            << renderer.native_depth_cold_animation_generation_ownership
            << ",\"bounds\":" << renderer.native_depth_cold_bounds
            << "}}"
            << "}}";
    }
    out << "]}";
    return out.str();
}

inline PlacedHitchExportResult export_placed_hitch_profile_recording(
    const std::string& root, const char* build, unsigned long process_id,
    std::uint32_t width, std::uint32_t height,
    const std::string& initial_scene_json,
    const PlacedHitchProfile& profile
#ifdef EID3D11_TESTING
    , PlacedHitchExportTestFailure injected_failure =
          PlacedHitchExportTestFailure::none
#endif
    ) noexcept {
    try {
        std::ostringstream out;
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.imbue(std::locale::classic());
        out << "{\"build\":\"" << (build ? build : "unknown")
            << "\",\"pid\":" << process_id
            << ",\"width\":" << width
            << ",\"height\":" << height
            << ",\"initial_scene\":"
            << (initial_scene_json.empty() ? "null" : initial_scene_json)
            << ",\"profile\":" << format_placed_hitch_profile_json(profile)
            << "}\n";
        return write_placed_hitch_profile_file(
            root, profile.sequence_id(), out.str()
#ifdef EID3D11_TESTING
            , injected_failure
#endif
            );
    } catch (const std::bad_alloc&) {
        PlacedHitchExportResult result{};
        result.status = PlacedHitchExportStatus::allocation_failed;
        result.error_code = ENOMEM;
        return result;
    } catch (...) {
        PlacedHitchExportResult result{};
        result.status = PlacedHitchExportStatus::serialization_failed;
        result.error_code = EIO;
        return result;
    }
}

} // namespace owned
