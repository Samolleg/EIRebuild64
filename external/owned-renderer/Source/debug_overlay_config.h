#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace owned {

enum class DebugOverlayMode : std::uint8_t {
    hidden = 0,
    compact = 1,
    full = 2,
};

enum class DebugResourceView : std::uint8_t {
    disabled = 0,
    raw_depth,
    linearized_depth,
    stencil,
    ambient_occlusion,
    normals,
    materials,
    object_ids,
    motion,
    shadows,
    shadow_cascade_coverage,
    hierarchical_depth,
    count,
};

// eid3d11.ini next to the renderer module.
std::wstring renderer_config_path();

const char* debug_resource_view_name(DebugResourceView view) noexcept;
const char* debug_resource_view_label(DebugResourceView view) noexcept;
bool parse_debug_resource_view(const char* value, DebugResourceView& view) noexcept;

struct DebugResourceViewAvailability {
    bool raw_depth = false;
    bool linearized_depth = false;
    bool stencil = false;
    bool ambient_occlusion = false;
    bool shadows = false;
    bool shadow_cascade_coverage = false;

    bool available(DebugResourceView view) const noexcept;
};

const char* debug_overlay_mode_name(DebugOverlayMode mode) noexcept;
bool parse_debug_overlay_mode(const char* value, DebugOverlayMode& mode) noexcept;
DebugOverlayMode next_debug_overlay_mode(DebugOverlayMode mode) noexcept;

constexpr bool debug_overlay_draws_software_cursor(DebugOverlayMode mode,
                                                   bool window_focused) noexcept {
    return window_focused && mode == DebugOverlayMode::full;
}

// The overlay pass runs for any ImGui window, the status notice, or critter
// labels, which stay on screen with every window closed (#68).
constexpr bool debug_overlay_pass_needed(bool window_visible, bool status_notice,
                                         bool critter_labels) noexcept {
    return window_visible || status_notice || critter_labels;
}

enum class RendererOptionType : std::uint8_t {
    boolean,
    integer,
    floating_point,
    text,
};

// Options are registered by the renderer only after the corresponding feature
// exists.  The overlay deliberately has no speculative graphics switches.
struct RendererOption {
    std::string key;
    std::string label;
    std::string description;
    std::string group;
    RendererOptionType type = RendererOptionType::boolean;
    bool restart_required = false;
    bool boolean_value = false;
    bool boolean_default = false;
    int integer_value = 0;
    int integer_default = 0;
    int integer_minimum = 0;
    int integer_maximum = 0;
    int integer_step = 1;
    float floating_value = 0.0f;
    float floating_default = 0.0f;
    float floating_minimum = 0.0f;
    float floating_maximum = 0.0f;
    float floating_step = 0.0f;
    std::string text_value;
};

class RendererConfig {
public:
    bool register_boolean(const char* key, const char* label, const char* group,
                          bool default_value, bool restart_required = false,
                          const char* description = nullptr);
    bool register_integer(const char* key, const char* label, const char* group,
                          int default_value, int minimum, int maximum, int step,
                          bool restart_required = false, const char* description = nullptr);
    bool register_floating_point(const char* key, const char* label, const char* group,
                                 float default_value, float minimum, float maximum, float step,
                                 bool restart_required = false,
                                 const char* description = nullptr);

    const std::vector<RendererOption>& options() const noexcept { return options_; }
    const RendererOption* find(const char* key) const noexcept;
    bool set_boolean(const char* key, bool value) noexcept;
    bool set_integer(const char* key, int value) noexcept;
    bool set_floating_point(const char* key, float value) noexcept;
    bool register_text(const char* key, const char* label, const char* group,
                       const char* description, const char* default_value = "");
    bool set_text(const char* key, const char* value);
    // Back to the registered default (boolean, integer, floating point).
    bool reset_to_default(const char* key) noexcept;

    DebugOverlayMode overlay_mode() const noexcept { return overlay_mode_; }
    void set_overlay_mode(DebugOverlayMode mode) noexcept;
    DebugResourceView debug_resource_view() const noexcept { return debug_resource_view_; }
    void set_debug_resource_view(DebugResourceView view) noexcept;
    // First-run graphics setup ([setup] first_run_done). Missing key or file:
    // not done. Only written once true.
    bool first_run_done() const noexcept { return first_run_done_; }
    void set_first_run_done(bool done) noexcept;
    // 8-hex id of this game's NVIDIA disk shader cache files ([setup]
    // nvidia_cache_id; landmine L-087), deleted at the next device creation.
    const std::string& nvidia_cache_id() const noexcept { return nvidia_cache_id_; }
    void set_nvidia_cache_id(const std::string& id);
    // Settings were saved past the VRAM budget and the session has not yet
    // exited cleanly ([setup] over_budget_pending; DebugOverlay owns it).
    bool over_budget_pending() const noexcept { return over_budget_pending_; }
    void set_over_budget_pending(bool pending) noexcept {
        if (over_budget_pending_ != pending) { over_budget_pending_ = pending; dirty_ = true; }
    }
    // The driver crashed in a parallel shader preparation: prepare on one
    // worker from now on ([setup] serial_shader_preparation; Renderer owns it).
    bool serial_shader_preparation() const noexcept { return serial_shader_preparation_; }
    void set_serial_shader_preparation(bool serial) noexcept {
        if (serial_shader_preparation_ != serial) { serial_shader_preparation_ = serial; dirty_ = true; }
    }
    // True when the loaded file carried [overlay] mode (players then keep the
    // developer monitor they chose; otherwise players start hidden).
    bool overlay_mode_loaded() const noexcept { return overlay_mode_loaded_; }
    // [setup] settings_version of the loaded file (0: missing, older build
    // or no file). Saving writes this value; the renderer's migration raises
    // it after dropping the values whose registered default changed since.
    int settings_version() const noexcept { return settings_version_; }
    void set_settings_version(int version) noexcept {
        if (settings_version_ != version) { settings_version_ = version; dirty_ = true; }
    }
    bool dirty() const noexcept { return dirty_; }
    void clear_dirty() noexcept { dirty_ = false; }

    // Local UTF-8 INI. Loading ignores unknown keys so newer and older builds
    // can share a file. Saving uses write-flush-atomic-replace semantics.
    bool load(const wchar_t* path);
    bool save(const wchar_t* path) const;

private:
    RendererOption* find_mutable(const char* key) noexcept;
    bool register_option(RendererOption option);

    std::vector<RendererOption> options_;
    // Key hash (FNV-1a 64) -> index into options_, so find is one lookup
    // plus one string compare; options are only ever appended. A second key
    // with an equal hash (never seen) keeps the linear search for itself.
    std::unordered_map<std::uint64_t, std::size_t> index_;
    bool index_collision_ = false;
    // Development default: keep the compact, no-input monitor visible until
    // the representative city/drawall performance gate is understood.
    DebugOverlayMode overlay_mode_ = DebugOverlayMode::compact;
    DebugResourceView debug_resource_view_ = DebugResourceView::disabled;
    bool first_run_done_ = false;
    bool over_budget_pending_ = false;
    bool serial_shader_preparation_ = false;
    std::string nvidia_cache_id_;
    bool overlay_mode_loaded_ = false;
    int settings_version_ = 0;
    bool dirty_ = false;
};

} // namespace owned
