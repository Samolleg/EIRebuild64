#pragma once

#include <cstddef>
#include <cstdint>

namespace owned {

class RendererConfig;

// Player graphics presets. The preset table in graphics_settings.cpp is the
// single place that says what each preset sets; High equals the registered
// option defaults, so a fresh eid3d11.ini reads as High.
enum class GraphicsPreset : std::uint8_t { low = 0, medium, high, ultra, custom };
constexpr int kGraphicsPresetCount = 4;

const char* graphics_preset_name(GraphicsPreset preset) noexcept;
const char* graphics_preset_summary(GraphicsPreset preset) noexcept;

// One player-facing option. Developer tuning, debug views and test toggles
// are deliberately absent; they stay in the developer window.
struct PlayerGraphicsSetting {
    const char* key;        // registered RendererConfig key
    const char* category;   // one of player_graphics_categories()
    const char* label;
    const char* tooltip;    // short plain-language explanation
    const char* choices;    // '\0'-separated labels for integer levels (index = value - minimum), or nullptr
    bool in_preset;         // false: a taste/comfort option the preset never touches
    float values[kGraphicsPresetCount];  // Low, Medium, High, Ultra (bool: 0/1)
};

const PlayerGraphicsSetting* player_graphics_settings(std::size_t& count) noexcept;
const char* const* player_graphics_categories(std::size_t& count) noexcept;

// Sets every preset key that the renderer registered; returns how many it set.
std::size_t apply_graphics_preset(RendererConfig& config, GraphicsPreset preset) noexcept;
// The preset whose every registered key matches the config, else custom.
GraphicsPreset detect_graphics_preset(const RendererConfig& config) noexcept;
// Settings migration only: when the config matches a preset of the table as
// it read in saved_version (rows added since are ignored), applies that preset's current values
// (rows that left the preset go back to their defaults) and returns it;
// otherwise changes nothing and returns custom.
GraphicsPreset reapply_graphics_preset_before(RendererConfig& config, int saved_version) noexcept;
// First-run suggestion from the adapter: integrated (UMA) GPUs get Low,
// discrete cards are graded by their WDDM local-memory budget (64-bit even
// in this 32-bit process). A rough hint only; the player chooses.
GraphicsPreset suggest_graphics_preset(bool integrated, std::uint64_t video_memory_bytes) noexcept;

// Display pacing. Both are player choices that default off and that no
// preset touches; developer mode forces both off so captures stay uncapped.
struct PresentationSettings {
    unsigned sync_interval = 0;  // Present sync interval (vsync)
    int frame_limit = 0;         // frames per second, 0 = uncapped
};
PresentationSettings presentation_settings(const RendererConfig& config, bool developer) noexcept;
// Frame-limit combo: Off, 30, 60, 90, 120, 144, 165, 240, then Custom.
constexpr int kFrameLimitChoices[] = {0, 30, 60, 90, 120, 144, 165, 240};
constexpr int kFrameLimitChoiceCount = 8;
constexpr int kFrameLimitCustomMinimum = 20;
int frame_limit_choice(int fps) noexcept;  // index into kFrameLimitChoices, or kFrameLimitChoiceCount (custom)

// Render scale: the world renders at this percent of the output size (each
// side) and is upscaled before the interface (scene_composite.h).
constexpr int kRenderScaleChoices[] = {50, 67, 75, 85, 100};
constexpr int kRenderScaleMinimum = 50;
constexpr unsigned render_scale_extent(unsigned output, int percent) noexcept {
    const int clamped = percent < kRenderScaleMinimum ? kRenderScaleMinimum : (percent > 100 ? 100 : percent);
    const unsigned extent = static_cast<unsigned>((std::uint64_t(output) * unsigned(clamped) + 50u) / 100u);
    return extent ? extent : 1u;
}

// Display mode, read from eid3d11.ini by the proxy when the game hands it the
// window (before the renderer exists); the settings window edits the same
// keys. Live Apply preserves the native interface canvas height and fonts,
// and maps that canvas to the new physical client size.
enum class DisplayMode : int { borderless = 0, windowed = 1 };
struct DisplaySettings {
    DisplayMode mode = DisplayMode::borderless;
    int window_width = 0;   // client size; 0 = the monitor's size
    int window_height = 0;
};
DisplaySettings read_display_settings(const wchar_t* ini_path) noexcept;
// Windowed client size: the requested size (0 = monitor size), shrunk to the
// largest client that fits the monitor work area once the frame is added.
struct ClientSize { int width = 0, height = 0; };
ClientSize windowed_client_size(const DisplaySettings& settings, int monitor_width, int monitor_height,
                                int max_client_width, int max_client_height) noexcept;

// EI_RENDERER_DEVELOPER=1 is set by the project's test launcher only.
bool renderer_developer_mode() noexcept;
constexpr bool first_run_settings_due(bool first_run_done, bool developer) noexcept {
    return !first_run_done && !developer;
}

} // namespace owned
