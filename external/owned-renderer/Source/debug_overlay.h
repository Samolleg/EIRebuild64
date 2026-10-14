#pragma once
#include "point_shadow_types.h"
#include "shadow_reuse_diagnostics.h"
#include "critter_summary.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>

#include "debug_overlay_config.h"
#include "graphics_settings.h"

namespace owned {

// Read the input owner registered on this HWND; no second control-state flag.
bool renderer_controls_capture_input(HWND window) noexcept;

struct RenderProfileSnapshot;
struct BridgeProfileSnapshot;
class RenderProfiler;

// Transient scalar summary for F10; the renderer retains ownership of the maps
// and caster revisions. No scene records or revision vectors are copied here.
struct DebugShadowSliceInfo {
    float radius = 0, world_units_per_texel = 0;
    std::array<float, 3> receiver_minimum{}, receiver_maximum{};
    std::array<float, 2> light_depth{};
    std::uint64_t caster_revision_entries = 0;
};
struct DebugShadowInfo {
    std::array<DebugShadowSliceInfo, 5> slices{};
    std::uint32_t enabled_mask = 0, valid_mask = 0, rewritten_mask = 0;
    bool feature_enabled = true;
    bool pass_in_progress = false;
};

struct RendererResourceSnapshot {
    std::uint64_t presentation_frame = 0;
    double present_interval_ms = 0.0;
    double present_smoothed_ms = 0.0;
    std::uint64_t rolling_frame_samples = 0;
    double rolling_frame_seconds = 0.0;
    double rolling_average_fps = 0.0;
    double rolling_lowest_fps = 0.0;
    bool rolling_frame_warming_up = true;
    bool rolling_frame_complete = false;
    bool rolling_frame_capacity_truncated = false;
    double process_cpu_percent = 0.0;
    std::uint64_t process_private_bytes = 0;
    double gpu_3d_percent = 0.0;
    bool process_cpu_valid = false;
    bool process_private_bytes_valid = false;
    bool gpu_3d_percent_valid = false;
    bool gpu_memory_valid = false;
    bool system_telemetry_stale = true;
    std::uint64_t system_telemetry_age_ms = 0;
    std::uint64_t gpu_budget_bytes = 0;
    std::uint64_t gpu_usage_bytes = 0;
    std::uint64_t renderer_resident_bytes = 0;
    std::uint64_t transient_upload_bytes = 0;
    std::uint64_t cpu_surface_bytes = 0;
    std::uint64_t gpu_texture_bytes = 0;
    std::uint64_t double_resident_bytes = 0;
    // GPU-primary surfaces whose CPU bytes are dropped, and the counted
    // staging readbacks that restored them (cumulative since start).
    std::uint64_t released_surface_bytes = 0;
    std::uint64_t released_surfaces = 0;
    std::uint64_t surface_restore_readbacks = 0;
    double surface_restore_readback_ms = 0.0;
    std::uint64_t pinned_surfaces = 0;
    std::uint64_t native_logical_draws = 0;
    std::uint64_t native_object_generic_ingress_calls = 0;
    std::uint64_t native_object_prepared_ingress_calls = 0;
    std::uint64_t native_physical_batches = 0;
    std::uint64_t native_reorderable_draws = 0;
    std::uint64_t native_ordered_draws = 0;
    std::uint64_t native_opaque_draws = 0;
    std::uint64_t native_cutout_draws = 0;
    std::uint64_t native_material_buckets = 0;
    std::uint64_t native_depth_selected = 0;
    std::uint64_t native_depth_warm = 0;
    std::uint64_t native_depth_cold = 0;
    std::uint64_t native_depth_registration_callbacks = 0;
    std::uint64_t native_shadow_draws = 0;
    std::uint64_t native_shadow_triangles = 0;
    std::uint64_t native_shadow_two_sided_cutout_draws = 0;
    std::uint64_t native_shadow_two_sided_cutout_selected_records = 0;
    std::uint64_t native_shadow_value_reuses = 0;
    std::uint64_t native_shadow_validity_scans = 0;
    std::uint64_t native_shadow_frame_copies = 0;
    std::uint8_t native_shadow_dirty_mask = 0;
    // F11 first-failure classification for the point cubemap reuse decision.
    NativePointShadowStats point_shadows{},point_shadows_cumulative{};
    std::uint64_t point_shadow_epoch=0,point_pass_skipped_frames=0,point_publication_missing_frames=0;
    ShadowInputDiagnostics shadow_input_cumulative{};
    ShadowRendererDiagnostics shadow_renderer_cumulative{};
    std::uint64_t native_point_shadow_cache_reuses = 0;
    std::uint64_t native_point_shadow_cache_unavailable = 0;
    std::uint64_t native_point_shadow_cache_frame_gap = 0;
    std::uint64_t native_point_shadow_cache_identity = 0;
    std::uint64_t native_point_shadow_cache_source = 0;
    std::uint64_t native_point_shadow_cache_dynamic = 0;
    std::uint64_t native_point_shadow_cache_terrain_time = 0;
    std::uint64_t native_shadow_source_world_serial = 0;
    std::uint64_t native_shadow_source_publications = 0;
    std::uint64_t native_shadow_source_active_visuals = 0, native_shadow_source_active_terrain = 0;
    std::uint64_t native_shadow_source_changed_visuals = 0, native_shadow_source_changed_terrain = 0;
    std::uint64_t native_shadow_source_static_casters_reused = 0;
    std::uint64_t native_shadow_source_part_bounds_rebuilt = 0;
    std::uint64_t native_shadow_source_receiver_refits = 0, native_shadow_source_caster_refits = 0;
    std::uint64_t native_shadow_source_map_reuses = 0;
    std::uint8_t native_shadow_source_dirty_mask = 0;
    bool native_shadows_enabled = true;
    bool culling_frozen = false;
    char fog_status[512] = "";  // world fog state line (renderer_fog.cpp)
    bool fog_problem = false;
    char sky_status[384] = "";  // owned sky state line (renderer_sky.cpp)
    bool sky_problem = false;
    char particle_status[256] = "";  // ambient particle pass line (renderer_ambient_particles.cpp)
    bool particle_problem = false;
    char critter_status[768] = "";
    char emissive_status[192] = "";  // emissive-surface lights (lava) of the active map
    CritterSummary critters{};   // F11 normal_frame.json "critters"; zero when off or stale
    std::uint64_t native_compute_dispatches = 0;
    std::uint64_t native_one_draw_batches = 0;
    // World X/Y point-light grid (point_light_grid.h): lights, grid size,
    // listed light references and the most lights any cell lists.
    struct { std::uint64_t lights = 0, width = 0, height = 0, references = 0, max_per_cell = 0, emissive = 0; } point_light_grid;
    std::uint64_t native_max_batch_draws = 0;
    std::uint64_t native_texture_array_attempts = 0;
    std::uint64_t native_texture_array_successes = 0;
    std::uint64_t native_texture_array_fallback_unavailable = 0;
    std::uint64_t native_texture_array_fallback_format = 0;
    std::uint64_t native_texture_array_invalid_layout = 0;
    std::uint64_t native_texture_array_allocation_failures = 0;
    std::uint64_t native_texture_array_classes = 0;
    std::uint64_t native_texture_array_pages = 0;
    std::uint64_t native_texture_array_slices = 0;
    std::uint64_t native_terrain_reorderable_contracts = 0;
    std::uint64_t native_terrain_preserve_contracts = 0;
    std::uint64_t native_terrain_textured_draws = 0;
    std::uint64_t native_terrain_untextured_draws = 0;
    std::uint64_t native_terrain_topology_uploads = 0;
    std::uint64_t native_terrain_topology_upload_vertices = 0;
    std::uint64_t native_terrain_color_only_uploads = 0;
    std::uint64_t native_terrain_color_only_upload_vertices = 0;
    std::uint64_t ui_blit_queue_cpu_qpc = 0;
    std::uint64_t ui_blit_queue_calls = 0;
    std::uint64_t ui_blit_conversion_cpu_qpc = 0;
    std::uint64_t ui_blit_conversion_calls = 0;
    std::uint64_t ui_blit_conversion_pixels = 0;
    std::uint32_t res_mapping_status = 0;
    bool res_mapping_hooks_active = false;
    std::uint64_t res_mapping_tracked_handles = 0;
    std::uint64_t res_mapping_logical_views = 0;
    std::uint64_t res_mapping_physical_views = 0;
    std::uint64_t res_mapping_physical_bytes = 0;
    std::uint64_t res_mapping_active_saved_bytes = 0;
    std::uint64_t res_mapping_peak_saved_bytes = 0;
    std::uint64_t res_mapping_shared_reuses = 0;
    std::uint64_t res_mapping_deferred_unmaps = 0;
    std::uint64_t res_mapping_passthrough_maps = 0;
    std::uint64_t res_mapping_identity_rejections = 0;
    std::uint64_t res_mapping_capacity_failures = 0;
    std::uint64_t res_mapping_validation_failures = 0;
    std::uint32_t res_cache_status = 0;
    bool res_cache_hooks_active = false;
    std::uint64_t res_cache_override_gets = 0;
    std::uint64_t res_cache_override_releases = 0;
    std::uint64_t res_cache_icon_releases = 0;
};

class DebugOverlay {
public:
    DebugOverlay();
    ~DebugOverlay();
    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    RendererConfig& config() noexcept { return config_; }
    const RendererConfig& config() const noexcept { return config_; }

    HRESULT initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context,
                       const wchar_t* config_path = nullptr);
    void shutdown() noexcept;

    // Call once, after the renderer's world and game UI passes and immediately
    // before Present. The upstream DX11 backend restores every state it changes;
    // this wrapper additionally preserves the shader stages it intentionally
    // clears. Hidden mode starts an ImGui frame only for visible combat UI.
    HRESULT render(const RenderProfileSnapshot* profile,
                   const BridgeProfileSnapshot* bridge,
                   const RendererResourceSnapshot& resources,
                   const DebugResourceViewAvailability& debug_views = {},
                   RenderProfiler* profiler = nullptr,
                   const DebugShadowInfo& shadows = {});
    void maintain() noexcept;
    void release_confinement() noexcept;
    // Shared by HWND notifications and the game's direct active-message call.
    // Only owned camp rails/wheel are consumed; other input remains native.
    bool process_camp_input(UINT message,WPARAM wparam,LPARAM lparam) noexcept;
    // The "Preparing shaders" progress window alone, over the target the caller
    // bound and cleared (Renderer::prepare_shaders). No input is taken.
    void render_shader_preparation(std::size_t done, std::size_t total) noexcept;

    // Developer monitor or full window only; the player settings window does
    // not turn the profiler on.
    bool wants_profiling() const noexcept;
    bool consume_capture_request() noexcept;
    bool consume_display_apply(DisplaySettings& settings) noexcept;
    void finish_display_apply(const DisplaySettings& settings, HRESULT result, const char* message) noexcept;
    // Shift+F11 or the overlay button: freeze/unfreeze the culling camera.
    bool consume_cull_freeze_toggle() noexcept;
    DebugOverlayMode mode() const noexcept { return config_.overlay_mode(); }
    void set_mode(DebugOverlayMode mode) noexcept;
    bool initialized() const noexcept;

    // Player Graphics Settings window (graphics_settings.h). Players toggle it
    // with F10 and reach the developer modes with Shift+F10; in developer mode
    // (EI_RENDERER_DEVELOPER=1) F10 keeps the developer cycle and Shift+F10
    // toggles this window. Not persisted.
    bool settings_open() const noexcept;
    // Any ImGui window is drawn this frame (developer mode or settings).
    bool visible() const noexcept;
    // Full window or settings window: the overlay owns keyboard and mouse.
    bool captures_input() const noexcept;
    bool captures_keyboard() const noexcept;
    bool process_combat_input(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    bool hud_editing() const noexcept;
    float hud_grid_step() const noexcept;
    // First-run setup is still owed: no [setup] first_run_done and not developer mode.
    bool first_run_pending() const noexcept;
    // The renderer calls this once a world (the main menu) is on screen.
    void open_first_run_settings() noexcept;
    // A one-line notice drawn over every frame in every mode (null clears),
    // for a state the player must not mistake for normal rendering.
    void set_status_notice(const char* text) noexcept { notice_ = text; }
    bool has_status_notice() const noexcept { return notice_ != nullptr; }

    struct Implementation;

private:
    std::unique_ptr<Implementation> implementation_;
    RendererConfig config_;
    const char* notice_ = nullptr;
};

} // namespace owned
