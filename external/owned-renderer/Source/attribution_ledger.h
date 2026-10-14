#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace owned {

constexpr std::uint32_t kAttributionMaximumScopes = 32;
// One marked window may retain at most 8191 frames. Reaching the fixed 8192-record
// capacity fails that window closed; the final slot proves capacity exhaustion.
constexpr std::uint32_t kAttributionRingCapacity = 8192;
constexpr std::uint32_t kAttributionMarkerCapacity = 64;

enum AttributionInvalid : std::uint32_t {
    attribution_valid = 0,
    attribution_first_partial = 1u << 0,
    attribution_query_failed = 1u << 1,
    attribution_present_exceeds_process = 1u << 2,
    attribution_negative_residual = 1u << 3,
    attribution_excess_residual = 1u << 4,
    attribution_open_scope = 1u << 5,
    attribution_scope_overflow = 1u << 6,
    attribution_cross_frame_scope = 1u << 7,
    attribution_mixed_serial = 1u << 8,
    attribution_dropped_record = 1u << 9,
    attribution_gpu_disjoint = 1u << 10,
    attribution_gpu_unresolved = 1u << 11,
    attribution_gpu_named_exceeds_span = 1u << 12,
    attribution_present_failed = 1u << 13,
    attribution_mixed_thread = 1u << 14,
    attribution_scope_reconciliation = 1u << 15,
};

struct AttributionCpuPoint {
    std::int64_t qpc = 0;
    std::uint64_t thread_kernel_100ns = 0;
    std::uint64_t thread_user_100ns = 0;
    std::uint64_t thread_cycles = 0;
    std::uint64_t process_kernel_100ns = 0;
    std::uint64_t process_user_100ns = 0;
    std::uint64_t process_cycles = 0;
    bool valid = false;
};

struct AttributionCpuDelta {
    std::int64_t qpc = 0;
    std::int64_t thread_kernel_100ns = 0;
    std::int64_t thread_user_100ns = 0;
    std::int64_t thread_cycles = 0;
    std::int64_t process_kernel_100ns = 0;
    std::int64_t process_user_100ns = 0;
    std::int64_t process_cycles = 0;
};

struct AttributionScopeRecord {
    std::array<char, 64> name{};
    std::uint64_t serial = 0;
    std::int64_t qpc_begin = 0;
    std::int64_t qpc_end = 0;
    AttributionCpuDelta cpu{};
    std::uint64_t gpu_ticks = 0;
    std::uint64_t upload_events = 0;
    std::uint64_t upload_bytes = 0;
    std::uint64_t draws = 0;
    std::uint64_t triangles = 0;
    std::uint64_t batches = 0;
    std::uint64_t capture_cpu_ticks = 0;
    std::uint64_t capture_calls = 0;
    std::uint64_t capture_bytes = 0;
    std::uint64_t packets_created = 0;
    std::uint64_t packets_appended = 0;
    bool gpu_resolved = false;
};

enum class RendererWorkloadField : std::uint32_t {
    native_opaque_draws_partial,
    native_cutout_draws_partial,
    world_draw_calls_partial,
    object_draw_calls_partial,
    simple_calls_partial,
    figure_placed_calls_partial,
    figure_lit_calls_partial,
    figure_gpu_owned_calls_partial,
    figure_cpu_fallback_calls_partial,
    terrain_calls_partial,
    terrain_outer_calls_partial,
    terrain_gpu_owned_calls_partial,
    terrain_cpu_fallback_calls_partial,
    terrain_candidate_sectors_partial,
    terrain_visible_sectors_partial,
    terrain_frustum_rejected_sectors_partial,
    profiler_observer_cpu_qpc,
    capture_marker_cpu_qpc,
    ui_blit_queue_cpu_qpc_partial,
    ui_blit_queue_calls_partial,
    ui_blit_conversion_cpu_qpc_partial,
    ui_blit_conversion_calls_partial,
    ui_blit_conversion_pixels_partial,
    // Temporary camp scroll observation; remove after the one-window decision.
    camp_scroll_valid,
    camp_scroll_mode,
    camp_scroll_generation,
    camp_scroll_first_qpc,
    camp_scroll_last_qpc,
    camp_scroll_item_calls,
    camp_scroll_item_wall_qpc,
    camp_scroll_item_max_wall_qpc,
    camp_scroll_miss_calls,
    camp_scroll_miss_wall_qpc,
    camp_scroll_miss_max_wall_qpc,
    camp_scroll_figure_misses,
    camp_scroll_stash_first,
    camp_scroll_stash_count,
    camp_scroll_stash_category,
    camp_scroll_stash_columns,
    camp_scroll_stash_rows,
    camp_scroll_vendor_first,
    camp_scroll_vendor_count,
    camp_scroll_vendor_category,
    camp_scroll_vendor_columns,
    camp_scroll_vendor_rows,
    count,
};

// One POD snapshot is embedded in the authoritative frame record. A clear
// presence bit means not_available; a set bit with value zero is measured zero.
struct RendererWorkloadSnapshot {
    std::uint64_t presence = 0;
    std::array<std::uint64_t,
        static_cast<std::size_t>(RendererWorkloadField::count)> values{};

    void set(RendererWorkloadField field, std::uint64_t value) noexcept {
        const auto index = static_cast<std::size_t>(field);
        presence |= std::uint64_t{1} << index;
        values[index] = value;
    }
    bool has(RendererWorkloadField field) const noexcept {
        return (presence & (std::uint64_t{1} << static_cast<std::size_t>(field))) != 0;
    }
    std::uint64_t get(RendererWorkloadField field) const noexcept {
        return values[static_cast<std::size_t>(field)];
    }
    bool active() const noexcept { return presence != 0; }
};
static_assert(std::is_trivially_copyable_v<RendererWorkloadSnapshot>);
static_assert(static_cast<std::size_t>(RendererWorkloadField::count) <= 64);

enum class AttributionMarkerLabel : std::uint32_t {
    main_menu_high_fps_ui,
    store_inventory,
    city_heavy_forced_visibility,
    outdoor_daytime,
    outdoor_nighttime,
    cave_interior,
    local_light_effect_torch_spell_quest_emitter_mutation,
    camera_pan_teleport,
    map_load_reload_transition_return_to_menu,
    camera_motion_rotation_zoom,
    camera_turns,
    terrain_foliage_water_vfx,
    animated_figures_equipment,
    interaction_text_ui,
    shadow_heavy_future,
    count,
};

enum class AttributionMarkerRepetition : std::uint32_t {
    none,
    first_observed,
    repeated_opening,
};

enum class AttributionIssue13MenuLabel : std::uint32_t {
    main_load,
    main_options,
    ingame_root,
    inventory,
    store,
    ingame_save,
    ingame_load,
    ingame_options,
    count,
};

enum class AttributionMarkerState : std::uint32_t {
    inactive,
    begin_armed,
    active,
    end_armed,
    complete,
    overflow,
    invalid,
};

struct AttributionMarkerSelection {
    AttributionMarkerLabel label = AttributionMarkerLabel::main_menu_high_fps_ui;
    bool issue13_menu_transition = false;
    std::array<char, 48> menu_label{};
    std::uint32_t opening_ordinal = 0;
    AttributionMarkerRepetition repetition = AttributionMarkerRepetition::none;
};

struct AttributionMarkerRecord {
    AttributionMarkerSelection selection{};
    std::uint64_t begin_frame_serial = 0;
    std::uint64_t end_frame_serial_exclusive = 0;
    std::int64_t begin_qpc = 0;
    std::int64_t end_qpc_exclusive = 0;
};

struct AttributionMarkerStatus {
    AttributionMarkerState state = AttributionMarkerState::inactive;
    std::uint32_t completed = 0;
    std::uint32_t capacity = kAttributionMarkerCapacity;
    AttributionMarkerSelection current{};
    bool window_flush_pending = false;
    std::uint32_t window_frame_count = 0;
    std::uint32_t window_frame_capacity = kAttributionRingCapacity;
};

const char* attribution_marker_label_name(AttributionMarkerLabel label) noexcept;
const char* attribution_marker_repetition_name(AttributionMarkerRepetition value) noexcept;
const char* attribution_issue13_menu_label_name(AttributionIssue13MenuLabel label) noexcept;
bool attribution_marker_selection_valid(const AttributionMarkerSelection& selection) noexcept;
bool attribution_capture_identity_qpc_valid(bool query_succeeded,
                                            std::int64_t qpc) noexcept;
bool attribution_marker_evidence_matches_capture_id(const char* evidence,
                                                     std::size_t evidence_size,
                                                     const char* capture_id) noexcept;

class AttributionMarkerRecorder {
public:
    bool arm_begin(const AttributionMarkerSelection& selection) noexcept;
    bool arm_end() noexcept;
    void successful_present_boundary(std::uint64_t next_frame_serial,
                                      std::int64_t qpc) noexcept;
    void fail_if_open() noexcept;
    void fail_capacity() noexcept;
    AttributionMarkerStatus status() const noexcept;
    std::size_t size() const noexcept { return size_; }
    const AttributionMarkerRecord& at(std::size_t index) const noexcept { return records_[index]; }
private:
    std::array<AttributionMarkerRecord, kAttributionMarkerCapacity> records_{};
    AttributionMarkerRecord active_{};
    AttributionMarkerSelection armed_{};
    std::size_t size_ = 0;
    AttributionMarkerState state_ = AttributionMarkerState::inactive;
};

struct AttributionFrameRecord {
    std::uint64_t serial = 0;
    std::uint64_t process_creation_filetime_100ns = 0;
    std::uint32_t pid = 0;
    std::uint32_t present_thread_id = 0;
    std::int64_t qpc_begin = 0;
    std::int64_t qpc_present_enter = 0;
    std::int64_t qpc_present_return = 0;
    std::uint64_t qpc_frequency = 0;
    std::uint32_t present_interval = 0;
    std::uint32_t present_flags = 0;
    std::int32_t present_hresult = 0;
    std::array<char, 48> renderer_commit{};
    std::array<char, 65> renderer_sha256{};
    std::array<char, 64> capture_id{};
    AttributionCpuDelta totals{};
    AttributionCpuDelta device{};
    AttributionCpuDelta named{};
    AttributionCpuDelta device_gap{};
    AttributionCpuDelta outside_device{};
    std::int64_t present_minus_process_kernel_100ns = 0;
    std::int64_t present_minus_process_user_100ns = 0;
    std::int64_t present_minus_process_cycles = 0;
    std::uint64_t gpu_span_ticks = 0;
    std::uint64_t gpu_named_ticks = 0;
    std::int64_t gpu_gap_ticks = 0;
    std::uint64_t gpu_frequency = 0;
    std::uint32_t invalid = attribution_first_partial;
    std::uint32_t scope_count = 0;
    std::array<AttributionScopeRecord, kAttributionMaximumScopes> scopes{};
    RendererWorkloadSnapshot renderer_workload{};
    bool capture_selected = false;
};

AttributionCpuDelta attribution_delta(const AttributionCpuPoint& begin,
                                      const AttributionCpuPoint& end) noexcept;
bool attribution_modes_compatible(bool wpr_active, bool suspend_sampler_requested) noexcept;
std::uint32_t validate_cpu_attribution(AttributionFrameRecord& record) noexcept;
std::uint32_t reconcile_gpu_attribution(AttributionFrameRecord& record,
                                        bool resolved, bool disjoint,
                                        std::uint64_t span_begin,
                                        std::uint64_t span_end,
                                        std::uint64_t named_ticks,
                                        std::uint64_t frequency) noexcept;

template <typename T, std::size_t Capacity>
class FixedAttributionRing {
public:
    bool push(const T& value) noexcept {
        if (size_ == Capacity) {
            overflowed_ = true;
            return false;
        }
        values_[(head_ + size_) % Capacity] = value;
        ++size_;
        return true;
    }
    const T& at(std::size_t index) const noexcept { return values_[(head_ + index) % Capacity]; }
    std::size_t size() const noexcept { return size_; }
    bool overflowed() const noexcept { return overflowed_; }
    void clear() noexcept { head_ = size_ = 0; overflowed_ = false; }
private:
    std::array<T, Capacity> values_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    bool overflowed_ = false;
};

class AttributionLedger {
public:
    AttributionLedger();
    ~AttributionLedger();
    bool initialize(const char* diagnostic_log_path = nullptr);
    void shutdown(const char* output_root);
    void record_timestamp_query_failure(const char* stage, HRESULT result) noexcept;
    bool enabled() const noexcept { return enabled_; }
    bool storage_allocated() const noexcept { return ring_ != nullptr; }
    bool interval_open() const noexcept { return interval_open_; }
    std::uint64_t serial() const noexcept { return current_.serial; }
    const AttributionCpuPoint& begin_point() const noexcept { return begin_; }
    const AttributionCpuPoint& pending_begin_point() const noexcept { return next_begin_; }
    bool has_pending_begin() const noexcept { return next_begin_valid_; }

    bool begin_interval(std::uint64_t serial);
    void record_device_begin();
    void record_device_end();
    void record_present_enter(std::uint32_t interval, std::uint32_t flags);
    void record_present_return(HRESULT result);
    void submit_first_partial(std::uint64_t serial, std::uint32_t interval,
                              std::uint32_t flags, HRESULT result) noexcept;
    AttributionCpuPoint sample_point() const noexcept;
    void add_scope(const char* name, const AttributionCpuPoint& begin,
                   const AttributionCpuPoint& end, std::uint64_t serial,
                   std::uint64_t upload_events, std::uint64_t upload_bytes,
                   std::uint64_t draws = 0, std::uint64_t triangles = 0,
                   std::uint64_t batches = 0, std::uint64_t capture_cpu_ticks = 0,
                   std::uint64_t capture_calls = 0, std::uint64_t capture_bytes = 0,
                   std::uint64_t packets_created = 0, std::uint64_t packets_appended = 0);
    void set_renderer_workload(const RendererWorkloadSnapshot& workload) noexcept;
    bool arm_marker_begin(const AttributionMarkerSelection& selection) noexcept;
    bool arm_marker_end() noexcept;
    AttributionMarkerStatus marker_status() const noexcept;
    bool marker_window_flush_pending() const noexcept { return window_flush_pending_; }
    const char* capture_id() const noexcept { return capture_id_.data(); }
    AttributionFrameRecord take_current() noexcept;
    bool submit(const AttributionFrameRecord& record) noexcept;
    bool finalize_if_full(const char* output_root) noexcept;
    bool flush_completed_window(const char* output_root) noexcept;
    void mark_dropped() noexcept { dropped_ = true; }
    void mark_invalid(std::uint32_t reason) noexcept { if (interval_open_) current_.invalid |= reason; }

private:
    bool flush(const char* output_root);
    bool flush_frames(const char* output_root);
    bool flush_markers(const char* directory);
    void disable_capture() noexcept;
    void configure_diagnostic_log(const char* path) noexcept;
    void diagnostic(const char* event, const char* format = nullptr, ...) noexcept;
    bool enabled_ = false;
    bool interval_open_ = false;
    bool dropped_ = false;
    bool capture_present_thread() noexcept;
    HANDLE present_thread_ = nullptr;
    DWORD present_thread_id_ = 0;
    AttributionCpuPoint begin_{};
    AttributionCpuPoint next_begin_{};
    AttributionCpuPoint device_begin_{};
    bool next_begin_valid_ = false;
    bool device_open_ = false;
    AttributionFrameRecord current_{};
    class Ring;
    std::unique_ptr<Ring> ring_{};
    std::array<char, 48> commit_{};
    std::array<char, 65> module_hash_{};
    std::array<char, 64> capture_id_{};
    std::uint64_t process_creation_filetime_100ns_ = 0;
    std::uint64_t qpc_frequency_ = 0;
    std::array<char, MAX_PATH> diagnostic_log_path_{};
    bool frames_diagnostic_logged_ = false;
    AttributionMarkerRecorder markers_{};
    std::uint64_t pending_marker_cpu_qpc_ = 0;
    bool pending_marker_cpu_qpc_valid_ = false;
    bool window_flush_pending_ = false;
    bool ledger_written_ = false;
    std::uint32_t window_frame_count_ = 0;
};

} // namespace owned
