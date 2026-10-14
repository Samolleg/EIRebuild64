#include "test_harness.h"
#include "debug_overlay.h"
#include "combat_ui.h"

#include "cursor_confinement.h"
#include "game_focus.h"
#include "render_profiler.h"
#include "engine_bridge.h"
#include "game_sites.h"
#include "graphics_settings.h"
#include "renderer_ui_ru.h"
#include "widescreen.h"
#include "world_fog.h"
#include "camp_inventory.h"

#include <dxgi1_4.h>

#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_dx11.h"
#include "../ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM wparam, LPARAM lparam);

namespace owned {
namespace {

constexpr UINT_PTR kOverlaySubclassId = 0x45494431u; // "EID1"
constexpr std::size_t kFrameHistorySize = 240;
constexpr std::array<int, 5> kMouseVirtualKeys{
    VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};

bool is_keyboard_message(UINT message) {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_HOTKEY;
}

bool is_mouse_message(UINT message) {
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_NCMOUSEMOVE ||
           message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP ||
           message == WM_NCRBUTTONDOWN || message == WM_NCRBUTTONUP ||
           message == WM_NCMBUTTONDOWN || message == WM_NCMBUTTONUP ||
           message == WM_SETCURSOR || message == WM_CAPTURECHANGED || message == 0xcc54u;
}

float overlay_scale_for_window(HWND window) {
    const float dpi_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window);
    if (!std::isfinite(dpi_scale)) return 1.0f;
    return std::clamp(dpi_scale, 1.0f, 3.0f);
}

// Window sizes offered in windowed mode: the monitor's 32-bit display modes,
// largest first, without duplicates (refresh rates).
std::vector<std::pair<int, int>> monitor_window_sizes(HWND window) {
    std::vector<std::pair<int, int>> sizes;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    const bool monitor_known = GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &info) != FALSE;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    try {
        // Window sizes need not be advertised as physical monitor modes.
        sizes = {{1280, 720}, {1280, 800}, {1920, 1080}, {1920, 1200},
                 {2560, 1440}, {2560, 1600}, {3440, 1440},
                 {3840, 1080}, {3840, 2160}, {5120, 1440}};
        for (DWORD index = 0; monitor_known && EnumDisplaySettingsExW(info.szDevice, index, &mode, 0); ++index) {
            const int width = static_cast<int>(mode.dmPelsWidth), height = static_cast<int>(mode.dmPelsHeight);
            if (mode.dmBitsPerPel != 32 || width < 800 || height < 600) continue;
            if (std::find(sizes.begin(), sizes.end(), std::make_pair(width, height)) == sizes.end())
                sizes.emplace_back(width, height);
        }
    } catch (const std::bad_alloc&) {
        sizes.clear();
    }
    std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) {
        return a.first * a.second != b.first * b.second ? a.first * a.second > b.first * b.second : a.first > b.first;
    });
    return sizes;
}

void format_window_size(char* label, std::size_t capacity, int width, int height) {
    struct Aspect { double ratio; const char* name; double tolerance; };
    constexpr Aspect aspects[]{
        {16.0 / 9, "16:9", .01}, {16.0 / 10, "16:10", .01},
        {4.0 / 3, "4:3", .01}, {5.0 / 4, "5:4", .01},
        {3.0 / 2, "3:2", .01}, {5.0 / 3, "5:3", .01},
        {1.0, "1:1", .01}, {32.0 / 9, "32:9", .01},
        {32.0 / 10, "32:10", .01},
        // Commercial 21:9 modes include 2560x1080, 3440x1440 and 3840x1600.
        {21.0 / 9, "21:9", .03}
    };
    const double ratio = static_cast<double>(width) / height;
    const char* aspect_name = nullptr;
    double best_error = 1.0;
    for (const auto& aspect : aspects) {
        const double error = std::fabs(ratio / aspect.ratio - 1.0);
        if (error <= aspect.tolerance && error < best_error) {
            aspect_name = aspect.name;
            best_error = error;
        }
    }
    if (aspect_name) std::snprintf(label, capacity, "%d x %d (%s)", width, height, aspect_name);
    else std::snprintf(label, capacity, "%d x %d (%.2f:1)", width, height, ratio);
}

int configured_value(const RendererConfig& config, const char* key) {
    const RendererOption* option = config.find(key);
    return option && option->type == RendererOptionType::integer ? option->integer_value : 0;
}

// An option that has no effect under the current value of the one it
// depends on: drawn greyed with a note, its own value kept for later.
const char* blocked_option_note(const RendererConfig& config, const char* key) {
    const auto is = [&](const char* value) { return std::strcmp(key, value) == 0; };
    const auto starts = [&](const char* value) { return std::strncmp(key, value, std::strlen(value)) == 0; };
    const auto boolean = [&](const char* value, bool fallback = false) {
        const auto* option = config.find(value);
        return option && option->type == RendererOptionType::boolean ? option->boolean_value : fallback;
    };
    const auto integer = [&](const char* value) { return configured_value(config, value); };
    if (boolean("water_simple") && (is("water_reflections") || is("water_reflection_quality") || is("water_physical")))
        return u8"\u043e\u0442\u043a\u043b\u044e\u0447\u0438\u0442\u0435 \u0432\u043e\u0434\u0443 \u0432 \u0441\u0442\u0438\u043b\u0435 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u0430";
    if (!boolean("water_reflections", true) && (is("water_reflection_quality") || is("water_physical")))
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u043e\u0442\u0440\u0430\u0436\u0435\u043d\u0438\u0439 \u0432 \u0432\u043e\u0434\u0435";
    if (!boolean("shadows", true) && (is("shadow_cascade_quality") || is("shadow_close_cascades") || is("shadow_far") || is("shadow_far_foliage_opaque") || is("shadow_sun_angle")))
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u043e\u043b\u043d\u0435\u0447\u043d\u044b\u0445 \u0442\u0435\u043d\u0435\u0439";
    if (is("shadow_far_foliage_opaque") && !boolean("shadow_far", true)) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0434\u0430\u043b\u044c\u043d\u0438\u0445 \u0442\u0435\u043d\u0435\u0439";
    if (!boolean("point_shadows", true) && starts("point_shadow_")) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0442\u0435\u043d\u0435\u0439 \u043e\u0442 \u043b\u0430\u043c\u043f \u0438 \u0444\u0430\u043a\u0435\u043b\u043e\u0432";
    if (is("shadow_softness") && !boolean("shadows", true) && !boolean("point_shadows", true))
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0442\u0435\u043d\u0435\u0439 \u043e\u0442 \u0441\u043e\u043b\u043d\u0446\u0430 \u0438\u043b\u0438 \u043b\u0430\u043c\u043f";
    if (integer("shadow_softness") == 0 && (is("shadow_sun_angle") || is("point_shadow_light_size")))
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u043c\u044f\u0433\u043a\u0438\u0445 \u0442\u0435\u043d\u0435\u0439";
    if ((is("terrain_antitile") || is("terrain_height_blend")) && integer("terrain_quality") == 0)
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0432\u044b\u0441\u043e\u043a\u043e\u0439 \u0434\u0435\u0442\u0430\u043b\u0438\u0437\u0430\u0446\u0438\u0438 \u043b\u0430\u043d\u0434\u0448\u0430\u0444\u0442\u0430";
    if (is("terrain_cliffs") && integer("terrain_blend") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u043d\u0438\u044f \u043f\u043e\u043a\u0440\u044b\u0442\u0438\u0439 \u0437\u0435\u043c\u043b\u0438";
    if (starts("cliff_") && (!boolean("terrain_cliffs", true) || integer("terrain_blend") == 0))
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0442\u0435\u043a\u0441\u0442\u0443\u0440 \u043e\u0431\u0440\u044b\u0432\u043e\u0432 \u0438 \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u043d\u0438\u044f \u043f\u043e\u043a\u0440\u044b\u0442\u0438\u0439";
    if (is("terrain_far_detile") && integer("terrain_blend") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u043d\u0438\u044f \u043f\u043e\u043a\u0440\u044b\u0442\u0438\u0439 \u0437\u0435\u043c\u043b\u0438";
    if (starts("terrain_bake_") && integer("terrain_bake") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u043f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043a\u0438 \u0442\u0435\u043a\u0441\u0442\u0443\u0440 \u043b\u0430\u043d\u0434\u0448\u0430\u0444\u0442\u0430";
    if ((starts("neighbour_fog_") || is("terrain_blend_neighbours") || is("terrain_bake_neighbours")) && !boolean("neighbour_maps", true))
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u043e\u0441\u0435\u0434\u043d\u0438\u0445 \u043a\u0430\u0440\u0442";
    if (is("terrain_blend_neighbours") && integer("terrain_blend") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u043d\u0438\u044f \u043f\u043e\u043a\u0440\u044b\u0442\u0438\u0439 \u0437\u0435\u043c\u043b\u0438";
    if (is("ground_cover_distance") && integer("ground_cover") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0442\u0440\u0430\u0432\u044b \u0438 \u043c\u0435\u043b\u043a\u043e\u0439 \u0440\u0430\u0441\u0442\u0438\u0442\u0435\u043b\u044c\u043d\u043e\u0441\u0442\u0438";
    if (starts("ambient_particle_") && integer("ambient_particles") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0447\u0430\u0441\u0442\u0438\u0446 \u043e\u043a\u0440\u0443\u0436\u0435\u043d\u0438\u044f";
    if (is("tree_sway_strength") && !boolean("tree_sway", true)) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u043a\u0430\u0447\u0430\u043d\u0438\u044f \u0434\u0435\u0440\u0435\u0432\u044c\u0435\u0432";
    if (starts("ao_") && !is("ao_quality") && integer("ao_quality") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0437\u0430\u0442\u0435\u043d\u0435\u043d\u0438\u044f \u0432 \u0443\u0433\u043b\u0443\u0431\u043b\u0435\u043d\u0438\u044f\u0445";
    if (starts("sky_") && !is("sky_clouds") && !is("sky_weather_test") && integer("sky_clouds") == 0)
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0430\u043d\u0438\u043c\u0438\u0440\u043e\u0432\u0430\u043d\u043d\u043e\u0433\u043e \u043d\u0435\u0431\u0430";
    if (is("sky_weather_test") && integer("sky_clouds") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0430\u043d\u0438\u043c\u0438\u0440\u043e\u0432\u0430\u043d\u043d\u043e\u0433\u043e \u043d\u0435\u0431\u0430; \u0442\u043e\u043b\u044c\u043a\u043e \u0432 \u0433\u043b\u0430\u0432\u043d\u043e\u043c \u043c\u0435\u043d\u044e";
    if (is("fog_volume_quality") && integer("fog_preset") < 2) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u043e\u0431\u044a\u0451\u043c\u043d\u043e\u0433\u043e \u0442\u0443\u043c\u0430\u043d\u0430";
    if (starts("fog_") && !is("fog_preset") && !is("fog_volume_quality") && integer("fog_preset") == 0)
        return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0430\u0442\u043c\u043e\u0441\u0444\u0435\u0440\u043d\u043e\u0433\u043e \u0438\u043b\u0438 \u043e\u0431\u044a\u0451\u043c\u043d\u043e\u0433\u043e \u0442\u0443\u043c\u0430\u043d\u0430";
    if (is("sun_shaft_strength") && integer("sun_shafts") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u043e\u043b\u043d\u0435\u0447\u043d\u044b\u0445 \u043b\u0443\u0447\u0435\u0439";
    if (is("bloom_strength") || is("bloom_threshold") || is("bloom_debug_view"))
        if (integer("bloom") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0441\u0432\u0435\u0447\u0435\u043d\u0438\u044f \u044f\u0440\u043a\u0438\u0445 \u043e\u0431\u043b\u0430\u0441\u0442\u0435\u0439";
    if (is("emissive_light_strength") && integer("emissive_lights") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u043e\u0441\u0432\u0435\u0449\u0435\u043d\u0438\u044f \u043e\u0442 \u0441\u0432\u0435\u0442\u044f\u0449\u0438\u0445\u0441\u044f \u043f\u043e\u0432\u0435\u0440\u0445\u043d\u043e\u0441\u0442\u0435\u0439";
    if (is("depth_of_field_strength") && integer("depth_of_field") == 0) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0433\u043b\u0443\u0431\u0438\u043d\u044b \u0440\u0435\u0437\u043a\u043e\u0441\u0442\u0438";
    if (is("lod_pixel_error") && !boolean("lod", true)) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0443\u043f\u0440\u043e\u0449\u0435\u043d\u0438\u044f \u0434\u0430\u043b\u044c\u043d\u0438\u0445 \u043e\u0431\u044a\u0435\u043a\u0442\u043e\u0432";
    if (starts("impostor_") && !boolean("impostors", true)) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0443\u043f\u0440\u043e\u0449\u0451\u043d\u043d\u044b\u0445 \u043a\u0440\u043e\u043d \u0434\u0435\u0440\u0435\u0432\u044c\u0435\u0432";
    if (const char* suffix = std::strstr(key, "_fade_transparency")) {
        const std::string parent = std::string(key, suffix) + "_auto_fade";
        if (!boolean(parent.c_str())) return u8"\u0442\u0440\u0435\u0431\u0443\u0435\u0442 \u0430\u0432\u0442\u043e\u0441\u043a\u0440\u044b\u0442\u0438\u044f \u044d\u0442\u043e\u0439 \u043f\u0430\u043d\u0435\u043b\u0438";
    }
    return nullptr;
}

double mib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

FigureFallbackReason dominant_figure_fallback(const BridgeProfileSnapshot& bridge,
                                               std::uint64_t& count) {
    FigureFallbackReason result = FigureFallbackReason::none;
    count = 0;
    for (std::size_t index = 1; index < bridge.figure_fallback_reasons.size(); ++index) {
        if (bridge.figure_fallback_reasons[index] <= count) continue;
        count = bridge.figure_fallback_reasons[index];
        result = static_cast<FigureFallbackReason>(index);
    }
    return result;
}

struct ExtraShaderStageState {
    ID3D11HullShader* hull = nullptr;
    ID3D11DomainShader* domain = nullptr;
    ID3D11ComputeShader* compute = nullptr;

    explicit ExtraShaderStageState(ID3D11DeviceContext* context) {
        if (!context) return;
        context->HSGetShader(&hull, nullptr, nullptr);
        context->DSGetShader(&domain, nullptr, nullptr);
        context->CSGetShader(&compute, nullptr, nullptr);
    }

    void restore(ID3D11DeviceContext* context) {
        if (context) {
            context->HSSetShader(hull, nullptr, 0);
            context->DSSetShader(domain, nullptr, 0);
            context->CSSetShader(compute, nullptr, 0);
        }
        if (hull) hull->Release();
        if (domain) domain->Release();
        if (compute) compute->Release();
        hull = nullptr;
        domain = nullptr;
        compute = nullptr;
    }
};

void help_marker(const char* text) {
    if (!text || !*text) return;
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace

struct DebugOverlay::Implementation {
    CombatUiRenderer combat_ui;
    HWND window = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ImGuiContext* imgui = nullptr;
    std::wstring config_path;
    std::array<float, kFrameHistorySize> cpu_history{};
    std::array<float, kFrameHistorySize> gpu_history{};
    std::array<float, kFrameHistorySize> present_history{};
    std::size_t history_count = 0;
    std::size_t history_cursor = 0;
    std::uint64_t last_profile_frame = ~std::uint64_t{0};
    bool capture_requested = false;
    // Frames of timing still to run before a capture asked for while the
    // overlay is hidden: the capture then carries a resolved normal frame.
    unsigned capture_warmup = 0;
    ULONGLONG capture_notice_until = 0;  // u8"\u041a\u0430\u0434\u0440 \u0437\u0430\u0445\u0432\u0430\u0447\u0435\u043d" notice, tick ms
    bool cull_freeze_toggle = false;
    bool pending_save = false;
    bool f10_was_down = false;
    bool f11_was_down = false;
    bool full_input_active = false;
    CursorConfinementState cursor_confinement{};
    int marker_label = 0;
    int marker_menu_label = 0;
    int marker_opening_ordinal = 1;
    int marker_repetition = 0;
    bool marker_issue13_transition = false;
    std::array<bool, kMouseVirtualKeys.size()> mouse_was_down{};
    float ui_scale = 1.0f;
    // Player Graphics Settings window.
    bool developer = false;
    bool settings_open = false;
    bool settings_first_run = false;
    bool settings_first_frame = false;
    bool hud_grid = false;
    bool panel_resizing = false;
    ImVec2 panel_drag_start{}, panel_size_start{};
    ImGuiTextFilter settings_filter;
    ImGuiTextFilter graphics_filter;
    ImGuiTextFilter diagnostics_filter;
    // The running game.exe has the 4 GB (large-address-aware) flag; else the
    // settings offer to patch it, and this holds the patch result.
    bool large_address_aware = true;
    const char* large_address_result = nullptr;
    int preset_slider = static_cast<int>(GraphicsPreset::high);
    GraphicsPreset suggested_preset = GraphicsPreset::custom;
    char adapter_name[128] = "";
    struct SavedValue { bool boolean; int integer; float floating; std::string text; };
    std::vector<SavedValue> settings_snapshot;  // option values (registration order) restored by Cancel
    std::array<int, 3> running_display{};  // last successfully applied settings
    bool display_apply_pending = false;
    DisplaySettings display_apply{};
    char display_apply_result[256] = "";
    std::vector<std::pair<int, int>> window_sizes;

    void remember(const RenderProfileSnapshot* profile,
                  const RendererResourceSnapshot& resources) {
        if (!profile || profile->frame == last_profile_frame) return;
        last_profile_frame = profile->frame;
        cpu_history[history_cursor] = static_cast<float>(std::max(0.0, profile->frame_cpu_ms));
        gpu_history[history_cursor] = static_cast<float>(
            profile->gpu_valid ? std::max(0.0, profile->frame_gpu_ms) : 0.0);
        present_history[history_cursor] = static_cast<float>(
            std::max(0.0, resources.present_smoothed_ms));
        history_cursor = (history_cursor + 1) % kFrameHistorySize;
        history_count = std::min(history_count + 1, kFrameHistorySize);
    }

    void linear_history(const std::array<float, kFrameHistorySize>& source,
                        std::array<float, kFrameHistorySize>& destination) const {
        const std::size_t start = history_count == kFrameHistorySize ? history_cursor : 0;
        for (std::size_t index = 0; index < history_count; ++index)
            destination[index] = source[(start + index) % kFrameHistorySize];
    }
};

namespace {

LRESULT CALLBACK overlay_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                       UINT_PTR, DWORD_PTR reference) {
    auto* overlay = reinterpret_cast<DebugOverlay*>(reference);
    if (!overlay || !overlay->initialized()) {
        camp_inventory_cancel_drag();return DefSubclassProc(window, message, wparam, lparam);
    }
    // Hover-only combat UI observes motion while gameplay keeps owning input.
    // Keep the backend's tracked position current after F10 has closed.
    if (!overlay->captures_input() && (message==WM_MOUSEMOVE || message==WM_MOUSELEAVE ||
        message==WM_NCMOUSEMOVE || message==WM_NCMOUSELEAVE))
        ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);
    // Camp rail ownership must end before focus swallowing or renderer controls
    // can hide a release. No native item, rotation or keyboard route is replaced.
    if(overlay->process_camp_input(message,wparam,lparam))return 0;
    if(overlay->process_combat_input(message,wparam,lparam))return 0;

    const bool focus_message = message == WM_ACTIVATE || message == WM_ACTIVATEAPP ||
        message == WM_KILLFOCUS || message == WM_SETFOCUS;
    if (focus_message)
        bridge_trace_input_focus(window, "before_game", message, wparam,
                                  static_cast<int>(overlay->mode()));
    if (cursor_confinement_release_message(message, wparam))
        overlay->release_confinement();
    // A running game keeps running across focus changes (background_mode):
    // its lost-focus path never sees the activation.
    if (game_focus_swallow_activation(window, message, wparam)) return 0;

    if (overlay->captures_input()) {
        const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        // Full mode and the settings window are explicit renderer-control
        // surfaces. Always consume their
        // keyboard and pointer messages instead of relying on the previous
        // frame's WantCapture flags, which leaves the first click exposed to
        // the legacy input path.
        if ((is_keyboard_message(message) && overlay->captures_keyboard()) || is_mouse_message(message))
            return handled ? handled : 1;
    }
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (focus_message)
        bridge_trace_input_focus(window, "after_game", message, wparam,
                                  static_cast<int>(overlay->mode()));
    return result;
}

void draw_live_statistics(const RendererResourceSnapshot& resources) {
    if (resources.rolling_frame_samples) {
        ImGui::Text(u8"\u0417\u0430 5 \u0441: \u0441\u0440\u0435\u0434\u043d\u0435\u0435 %.1f FPS  \u043c\u0438\u043d\u0438\u043c\u0443\u043c %.1f FPS", resources.rolling_average_fps,
                    resources.rolling_lowest_fps);
    } else {
        ImGui::TextUnformatted(u8"\u0417\u0430 5 \u0441: \u0441\u0440\u0435\u0434\u043d\u0435\u0435 \u043d/\u0434  \u043c\u0438\u043d\u0438\u043c\u0443\u043c \u043d/\u0434");
    }
    if (resources.culling_frozen)
        ImGui::TextColored(ImVec4(1.f, .35f, .2f, 1.f), u8"\u041a\u0410\u041c\u0415\u0420\u0410 \u041e\u0422\u0421\u0415\u0427\u0415\u041d\u0418\u042f \u0417\u0410\u041a\u0420\u0415\u041f\u041b\u0415\u041d\u0410 (Shift+F11)");
    if (resources.fog_status[0])
        ImGui::TextColored(resources.fog_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.fog_status);
    if (resources.sky_status[0])
        ImGui::TextColored(resources.sky_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.sky_status);
    if (resources.particle_status[0])
        ImGui::TextColored(resources.particle_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.particle_status);
    if (resources.critter_status[0]) ImGui::TextUnformatted(resources.critter_status);
    if (resources.emissive_status[0]) ImGui::TextColored(ImVec4(1.f, .62f, .3f, 1.f), "%s", resources.emissive_status);
    ImGui::Text(u8"\u0422\u0435\u043d\u0438 %s  \u043e\u0442\u0440\u0438\u0441\u043e\u0432\u043e\u043a \u0433\u043b\u0443\u0431\u0438\u043d\u044b %llu  \u0442\u0440\u0435\u0443\u0433\u043e\u043b\u044c\u043d\u0438\u043a\u043e\u0432 %llu",
                resources.native_shadows_enabled ? "ON" : "OFF",
                static_cast<unsigned long long>(resources.native_shadow_draws),
                static_cast<unsigned long long>(resources.native_shadow_triangles));
    const auto& point=resources.point_shadows;
    ImGui::Text(u8"\u041b\u0430\u043c\u043f\u044b: \u043f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043b\u0435\u043d\u043e %llu  \u043e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u043e %llu  \u043d\u0430\u043b\u043e\u0436\u0435\u043d\u0438\u0439 %llu  \u0432\u043d\u0435 \u0431\u044e\u0434\u0436\u0435\u0442\u0430 %llu",
        point.baked,point.rebaked,point.overlays,point.budget);
    ImGui::Text(u8"\u0421\u0431\u0440\u043e\u0441 \u0442\u0435\u043d\u0435\u0439 \u043b\u0430\u043c\u043f: \u0440\u0430\u0434\u0438\u0443\u0441 %llu  \u0437\u0435\u043c\u043b\u044f %llu  \u0441\u0432\u0435\u0442 %llu  \u0441\u043f\u0438\u0441\u043e\u043a %llu  \u0441\u0431\u0440\u043e\u0441 %llu",
        point.radius_dirty,point.terrain,point.light,point.list,point.reset);
    ImGui::Text(u8"\u041f\u0430\u043c\u044f\u0442\u044c \u0442\u0435\u043d\u0435\u0439 \u043b\u0430\u043c\u043f: \u043e\u0448\u0438\u0431\u043e\u043a \u0432\u044b\u0434\u0435\u043b\u0435\u043d\u0438\u044f %llu  \u043e\u0441\u0432\u043e\u0431\u043e\u0436\u0434\u0435\u043d\u0438\u0439 %llu",
        point.allocation_failures,point.pressure_drops);
    if (resources.rolling_frame_capacity_truncated) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.18f, 1.0f));
        ImGui::TextUnformatted(u8"\u041e\u043a\u043d\u043e 5 \u0441 \u0441\u043e\u043a\u0440\u0430\u0449\u0435\u043d\u043e: \u0447\u0430\u0441\u0442\u043e\u0442\u0430 \u043a\u0430\u0434\u0440\u043e\u0432 \u043f\u0440\u0435\u0432\u044b\u0448\u0430\u0435\u0442 \u0432\u043c\u0435\u0441\u0442\u0438\u043c\u043e\u0441\u0442\u044c \u0432\u044b\u0431\u043e\u0440\u043a\u0438");
        ImGui::PopStyleColor();
    } else if (resources.rolling_frame_warming_up) {
        ImGui::Text(u8"\u041d\u0430\u043a\u043e\u043f\u043b\u0435\u043d\u0438\u0435 \u043e\u043a\u043d\u0430 5 \u0441: %.1f / 5,0 \u0441", resources.rolling_frame_seconds);
    }

    if (resources.system_telemetry_stale) {
        if (resources.system_telemetry_age_ms)
            ImGui::Text(u8"CPU \u0438\u0433\u0440\u044b \u043d/\u0434  GPU 3D \u043d/\u0434  (\u0434\u0430\u043d\u043d\u044b\u0435 \u0443\u0441\u0442\u0430\u0440\u0435\u043b\u0438: %llu \u043c\u0441)",
                        static_cast<unsigned long long>(resources.system_telemetry_age_ms));
        else
            ImGui::TextUnformatted(u8"CPU \u0438\u0433\u0440\u044b \u043d/\u0434  GPU 3D \u043d/\u0434  (\u0441\u0431\u043e\u0440 \u0434\u0430\u043d\u043d\u044b\u0445)");
    } else if (resources.process_cpu_valid && resources.gpu_3d_percent_valid) {
        ImGui::Text(u8"CPU \u0438\u0433\u0440\u044b %.1f%%  GPU 3D %.1f%%", resources.process_cpu_percent,
                    resources.gpu_3d_percent);
    } else if (resources.process_cpu_valid) {
        ImGui::Text(u8"CPU \u0438\u0433\u0440\u044b %.1f%%  GPU 3D \u043d/\u0434", resources.process_cpu_percent);
    } else if (resources.gpu_3d_percent_valid) {
        ImGui::Text(u8"CPU \u0438\u0433\u0440\u044b \u043d/\u0434  GPU 3D %.1f%%", resources.gpu_3d_percent);
    } else {
        ImGui::TextUnformatted(u8"CPU \u0438\u0433\u0440\u044b \u043d/\u0434  GPU 3D \u043d/\u0434");
    }

    if (!resources.system_telemetry_stale && resources.process_private_bytes_valid &&
        resources.gpu_memory_valid) {
        ImGui::Text(u8"RAM \u043f\u0440\u043e\u0446\u0435\u0441\u0441\u0430 %.1f \u041c\u0438\u0411  \u0432\u0438\u0434\u0435\u043e\u043f\u0430\u043c\u044f\u0442\u044c %.1f / %.1f \u041c\u0438\u0411",
                    mib(resources.process_private_bytes), mib(resources.gpu_usage_bytes),
                    mib(resources.gpu_budget_bytes));
    } else if (!resources.system_telemetry_stale && resources.process_private_bytes_valid) {
        ImGui::Text(u8"RAM \u043f\u0440\u043e\u0446\u0435\u0441\u0441\u0430 %.1f \u041c\u0438\u0411  \u0432\u0438\u0434\u0435\u043e\u043f\u0430\u043c\u044f\u0442\u044c \u043d/\u0434",
                    mib(resources.process_private_bytes));
    } else if (!resources.system_telemetry_stale && resources.gpu_memory_valid) {
        ImGui::Text(u8"RAM \u043f\u0440\u043e\u0446\u0435\u0441\u0441\u0430 \u043d/\u0434  \u0432\u0438\u0434\u0435\u043e\u043f\u0430\u043c\u044f\u0442\u044c %.1f / %.1f \u041c\u0438\u0411",
                    mib(resources.gpu_usage_bytes), mib(resources.gpu_budget_bytes));
    } else {
        ImGui::TextUnformatted(u8"RAM \u043f\u0440\u043e\u0446\u0435\u0441\u0441\u0430 \u043d/\u0434  \u0432\u0438\u0434\u0435\u043e\u043f\u0430\u043c\u044f\u0442\u044c \u043d/\u0434");
    }
}

void draw_compact_monitor(const RenderProfileSnapshot* profile, const BridgeProfileSnapshot* bridge,
                          const RendererResourceSnapshot& resources, float ui_scale) {
    ImGui::SetNextWindowPos(ImVec2(10.0f * ui_scale, 10.0f * ui_scale), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("EID3D11 performance", nullptr, flags)) {
        ImGui::End();
        return;
    }
    draw_live_statistics(resources);
    if (!profile) {
        ImGui::TextUnformatted(u8"EID3D11  /  \u0441\u0431\u043e\u0440 \u0434\u0430\u043d\u043d\u044b\u0445 \u043f\u0440\u043e\u0444\u0438\u043b\u0438\u0440\u043e\u0432\u0449\u0438\u043a\u0430...");
    } else {
        const double interval_ms = resources.present_smoothed_ms > 0.0
            ? resources.present_smoothed_ms
            : (bridge && bridge->begin_interval_ms > 0.0
                ? bridge->begin_interval_ms : 0.0);
        const double fps = interval_ms > 0.0 ? 1000.0 / interval_ms : 0.0;
        if (profile->gpu_valid)
            ImGui::Text(u8"EID3D11  %.0f FPS / %.2f \u043c\u0441  CPU \u0440\u0435\u043d\u0434\u0435\u0440\u0430 %.2f \u043c\u0441  GPU %.2f \u043c\u0441",
                        fps, interval_ms, profile->frame_cpu_ms, profile->frame_gpu_ms);
        else
            ImGui::Text(u8"EID3D11  %.0f FPS / %.2f \u043c\u0441  CPU \u0440\u0435\u043d\u0434\u0435\u0440\u0430 %.2f \u043c\u0441  GPU \u043d/\u0434",
                        fps, interval_ms, profile->frame_cpu_ms);
        std::uint64_t draws = 0, triangles = 0, batches = 0;
        for (UINT index = 0; index < profile->pass_count && index < profile->passes.size(); ++index) {
            draws += profile->passes[index].counters.draws;
            triangles += profile->passes[index].counters.triangles;
            batches += profile->passes[index].counters.batches;
        }
        ImGui::Text(u8"\u041e\u0442\u0440\u0438\u0441\u043e\u0432\u043e\u043a %llu  \u0442\u0440\u0435\u0443\u0433\u043e\u043b\u044c\u043d\u0438\u043a\u043e\u0432 %llu  \u043f\u0430\u043a\u0435\u0442\u043e\u0432 %llu",
                    static_cast<unsigned long long>(draws),
                    static_cast<unsigned long long>(triangles),
                    static_cast<unsigned long long>(batches));
        ImGui::Text(u8"\u0423\u0447\u0442\u0435\u043d\u043e \u0440\u0435\u043d\u0434\u0435\u0440\u043e\u043c %.0f \u041c\u0438\u0411", mib(resources.renderer_resident_bytes));
        if (bridge) {
            ImGui::Text(u8"API %.2f \u043c\u0441  \u0432\u043d\u0435 API %.2f \u043c\u0441", bridge->frame_cpu_ms,
                        bridge->outside_entry_cpu_ms);
            ImGui::Text(u8"3D-\u0441\u0446\u0435\u043d\u0430 %.2f \u043c\u0441  \u043d\u0435 \u0443\u0447\u0442\u0435\u043d\u043e %.2f \u043c\u0441  \u043f\u0440\u043e\u0447\u0435\u0435 %.2f \u043c\u0441",
                        bridge->scene_3d_cpu_ms,
                        bridge->scene_3d_unattributed_cpu_ms,
                        bridge->outside_scene_3d_cpu_ms);
            ImGui::Text(u8"\u041f\u0440\u043e\u0441\u0442\u0430\u044f \u0433\u0435\u043e\u043c\u0435\u0442\u0440\u0438\u044f %.2f \u043c\u0441 / %llu  \u043c\u043e\u0434\u0435\u043b\u0438 %.2f \u043c\u0441 / %llu",
                        bridge->simple_cpu_ms,
                        static_cast<unsigned long long>(bridge->simple_calls),
                        bridge->figure_placed_cpu_ms + bridge->figure_lit_cpu_ms,
                        static_cast<unsigned long long>(bridge->figure_placed_calls +
                                                        bridge->figure_lit_calls));
            ImGui::Text("GPU figures %llu  CPU fallback %llu",
                        static_cast<unsigned long long>(bridge->figure_gpu_owned_calls),
                        static_cast<unsigned long long>(bridge->figure_cpu_fallback_calls));
            ImGui::Text(u8"\u041b\u0430\u043d\u0434\u0448\u0430\u0444\u0442 %.2f \u043c\u0441 / %llu  GPU %llu  \u0430\u043b\u044c\u0442\u0435\u0440\u043d\u0430\u0442\u0438\u0432\u043d\u044b\u0439 \u043f\u0443\u0442\u044c %llu",
                        bridge->terrain_cpu_ms,
                        static_cast<unsigned long long>(bridge->terrain_calls),
                        static_cast<unsigned long long>(bridge->terrain_gpu_owned_calls),
                        static_cast<unsigned long long>(bridge->terrain_cpu_fallback_calls));
            ImGui::Text(u8"\u041b\u0430\u043d\u0434\u0448\u0430\u0444\u0442: \u043e\u0431\u0449\u0438\u0439 %.2f \u043c\u0441  \u0437\u0430\u0445\u0432\u0430\u0442 %.2f \u043c\u0441  \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0430 %.2f \u043c\u0441",
                        bridge->terrain_outer_cpu_ms, bridge->terrain_capture_cpu_ms,
                        bridge->terrain_submit_cpu_ms);
            ImGui::Text(u8"\u041f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043a\u0430/\u043f\u043e\u0432\u0442\u043e\u0440/\u043f\u0440\u043e\u043f\u0443\u0441\u043a/\u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b: %llu / %llu / %llu / %llu",
                        static_cast<unsigned long long>(bridge->terrain_owned_frame_preparations),
                        static_cast<unsigned long long>(bridge->terrain_owned_transparent_reuses),
                        static_cast<unsigned long long>(bridge->terrain_owned_omitted_ranges),
                        static_cast<unsigned long long>(
                            bridge->terrain_original_render_front_end_calls));
            ImGui::Text(u8"\u0421\u0435\u043a\u0442\u043e\u0440\u044b: \u043a\u0430\u043d\u0434\u0438\u0434\u0430\u0442\u044b/\u0432\u0438\u0434\u0438\u043c\u044b\u0435/\u043e\u0442\u0441\u0435\u0447\u0451\u043d\u043d\u044b\u0435: %llu / %llu / %llu",
                        static_cast<unsigned long long>(bridge->terrain_candidate_sectors),
                        static_cast<unsigned long long>(bridge->terrain_visible_sectors),
                        static_cast<unsigned long long>(bridge->terrain_frustum_rejected_sectors));
            ImGui::Text("GPU anim %llu  waves %llu  tiles %llu  fallback %llu",
                        static_cast<unsigned long long>(bridge->terrain_animation_owned_updates),
                        static_cast<unsigned long long>(bridge->terrain_wave_sectors),
                        static_cast<unsigned long long>(bridge->terrain_animated_tile_updates),
                        static_cast<unsigned long long>(bridge->terrain_animation_fallback_updates));
            std::uint64_t dominant_count = 0;
            const auto dominant = dominant_figure_fallback(*bridge, dominant_count);
            if (dominant_count || bridge->figure_gpu_submit_failures ||
                bridge->figure_gpu_culled_calls)
                ImGui::Text(u8"\u0410\u043b\u044c\u0442\u0435\u0440\u043d\u0430\u0442\u0438\u0432\u043d\u044b\u0439 \u043f\u0443\u0442\u044c: %s %llu  \u043e\u0448\u0438\u0431\u043e\u043a \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0438 %llu  \u043e\u0442\u0441\u0435\u0447\u0435\u043d\u043e %llu",
                            figure_fallback_reason_name(dominant),
                            static_cast<unsigned long long>(dominant_count),
                            static_cast<unsigned long long>(bridge->figure_gpu_submit_failures),
                            static_cast<unsigned long long>(bridge->figure_gpu_culled_calls));
        }
    }
    ImGui::End();
}

void draw_performance_tab(DebugOverlay::Implementation& state, const RenderProfileSnapshot* profile,
                          const BridgeProfileSnapshot* bridge,
                          const RendererResourceSnapshot& resources) {
    draw_live_statistics(resources);
    ImGui::Separator();
    if (!profile) {
        ImGui::TextUnformatted(u8"\u041f\u0440\u043e\u0444\u0438\u043b\u0438\u0440\u043e\u0432\u0449\u0438\u043a \u0441\u043e\u0431\u0438\u0440\u0430\u0435\u0442 \u0434\u0430\u043d\u043d\u044b\u0435. \u0417\u0430\u043c\u0435\u0440\u044b GPU \u043f\u0440\u0438\u0445\u043e\u0434\u044f\u0442 \u0441 \u0437\u0430\u0434\u0435\u0440\u0436\u043a\u043e\u0439, \u043d\u0435 \u0431\u043b\u043e\u043a\u0438\u0440\u0443\u044f \u043a\u0430\u0434\u0440.");
        return;
    }
    const double frame_ms = resources.present_smoothed_ms;
    const double fps = frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0;
    ImGui::Text(u8"\u041a\u0430\u0434\u0440 %llu  /  \u043f\u043e\u043a\u0430\u0437 %.1f FPS / %.3f \u043c\u0441  /  CPU \u0440\u0435\u043d\u0434\u0435\u0440\u0430 %.3f \u043c\u0441",
                static_cast<unsigned long long>(profile->frame), fps, frame_ms,
                profile->frame_cpu_ms);
    ImGui::SameLine();
    if (profile->gpu_valid) ImGui::Text(u8"/  GPU %.3f \u043c\u0441", profile->frame_gpu_ms);
    else ImGui::TextUnformatted("|  GPU unavailable");

    std::array<float, kFrameHistorySize> cpu{};
    std::array<float, kFrameHistorySize> gpu{};
    std::array<float, kFrameHistorySize> present{};
    state.linear_history(state.cpu_history, cpu);
    state.linear_history(state.gpu_history, gpu);
    state.linear_history(state.present_history, present);
    const int count = static_cast<int>(state.history_count);
    ImGui::PlotLines(u8"\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b \u043a\u0430\u0434\u0440\u043e\u0432 (\u043c\u0441)", present.data(), count, 0, nullptr, 0.0f, 33.4f,
                     ImVec2(-1.0f, 72.0f * state.ui_scale));
    ImGui::PlotLines(u8"\u0412\u0440\u0435\u043c\u044f CPU \u0440\u0435\u043d\u0434\u0435\u0440\u0430 (\u043c\u0441)", cpu.data(), count, 0, nullptr, 0.0f, 33.4f,
                     ImVec2(-1.0f, 72.0f * state.ui_scale));
    if (profile->gpu_valid)
        ImGui::PlotLines("GPU frame (ms)", gpu.data(), count, 0, nullptr, 0.0f, 33.4f,
                         ImVec2(-1.0f, 72.0f * state.ui_scale));

    if (ImGui::BeginTable(u8"\u041f\u0440\u043e\u0445\u043e\u0434\u044b", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                           ImVec2(0.0f, 270.0f * state.ui_scale))) {
        ImGui::TableSetupColumn(u8"\u041f\u0440\u043e\u0445\u043e\u0434");
        ImGui::TableSetupColumn("CPU ms");
        ImGui::TableSetupColumn("GPU ms");
        ImGui::TableSetupColumn(u8"\u041e\u0442\u0440\u0438\u0441\u043e\u0432\u043a\u0438");
        ImGui::TableSetupColumn(u8"\u0422\u0440\u0435\u0443\u0433\u043e\u043b\u044c\u043d\u0438\u043a\u0438");
        ImGui::TableSetupColumn(u8"\u041f\u0430\u043a\u0435\u0442\u044b");
        ImGui::TableSetupColumn(u8"\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0438");
        ImGui::TableSetupColumn(u8"\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430, \u041c\u0438\u0411");
        ImGui::TableHeadersRow();
        for (UINT index = 0; index < profile->pass_count && index < profile->passes.size(); ++index) {
            const auto& pass = profile->passes[index];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(pass.name.data());
            ImGui::TableNextColumn(); ImGui::Text("%.3f", pass.cpu_ms);
            ImGui::TableNextColumn();
            if (profile->gpu_valid && pass.gpu_ms >= 0.0) ImGui::Text("%.3f", pass.gpu_ms);
            else ImGui::TextUnformatted("-");
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(pass.counters.draws));
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(pass.counters.triangles));
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(pass.counters.batches));
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(pass.counters.uploads));
            ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(pass.counters.upload_bytes));
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText(u8"\u041f\u0430\u043c\u044f\u0442\u044c");
    if (resources.gpu_memory_valid) {
        ImGui::Text(u8"\u041b\u043e\u043a\u0430\u043b\u044c\u043d\u044b\u0439 \u0431\u044e\u0434\u0436\u0435\u0442 WDDM: %.1f \u041c\u0438\u0411", mib(resources.gpu_budget_bytes));
        ImGui::Text(u8"\u041b\u043e\u043a\u0430\u043b\u044c\u043d\u0430\u044f \u043f\u0430\u043c\u044f\u0442\u044c \u043f\u0440\u043e\u0446\u0435\u0441\u0441\u0430: %.1f \u041c\u0438\u0411", mib(resources.gpu_usage_bytes));
    } else {
        ImGui::TextUnformatted(u8"\u0414\u0430\u043d\u043d\u044b\u0435 \u0431\u044e\u0434\u0436\u0435\u0442\u0430 WDDM \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b");
    }
    ImGui::Text(u8"\u0423\u0447\u0442\u0451\u043d\u043d\u044b\u0435 \u0440\u0435\u0441\u0443\u0440\u0441\u044b \u0440\u0435\u043d\u0434\u0435\u0440\u0430: %.1f \u041c\u0438\u0411", mib(resources.renderer_resident_bytes));
    ImGui::Text(u8"\u0412\u0440\u0435\u043c\u0435\u043d\u043d\u0430\u044f \u043f\u0430\u043c\u044f\u0442\u044c \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0438: %.1f \u041c\u0438\u0411", mib(resources.transient_upload_bytes));
    ImGui::Text(u8"\u041f\u043e\u0432\u0435\u0440\u0445\u043d\u043e\u0441\u0442\u0438 \u0432 \u043f\u0430\u043c\u044f\u0442\u0438 CPU: %.1f \u041c\u0438\u0411", mib(resources.cpu_surface_bytes));
    ImGui::Text(u8"\u0422\u0435\u043a\u0441\u0442\u0443\u0440\u044b \u0432 \u043f\u0430\u043c\u044f\u0442\u0438 GPU: %.1f \u041c\u0438\u0411", mib(resources.gpu_texture_bytes));
    if (resources.double_resident_bytes) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.18f, 1.0f));
        ImGui::Text(u8"\u041f\u043e\u0441\u0442\u043e\u044f\u043d\u043d\u044b\u0435 \u043a\u043e\u043f\u0438\u0438 CPU + GPU: %.1f \u041c\u0438\u0411", mib(resources.double_resident_bytes));
        ImGui::PopStyleColor();
    }
    ImGui::Text(u8"\u041f\u043e\u0432\u0435\u0440\u0445\u043d\u043e\u0441\u0442\u0435\u0439 \u0442\u043e\u043b\u044c\u043a\u043e \u043d\u0430 GPU: %llu (\u043e\u0441\u0432\u043e\u0431\u043e\u0436\u0434\u0435\u043d\u043e %.1f \u041c\u0438\u0411 CPU), \u0437\u0430\u043a\u0440\u0435\u043f\u043b\u0435\u043d\u043e %llu",
                static_cast<unsigned long long>(resources.released_surfaces),
                mib(resources.released_surface_bytes),
                static_cast<unsigned long long>(resources.pinned_surfaces));
    ImGui::Text(u8"\u0412\u043e\u0441\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d\u0438\u0439 \u043f\u043e\u0432\u0435\u0440\u0445\u043d\u043e\u0441\u0442\u0435\u0439: %llu (\u0432\u0441\u0435\u0433\u043e %.2f \u043c\u0441)",
                static_cast<unsigned long long>(resources.surface_restore_readbacks),
                resources.surface_restore_readback_ms);

    ImGui::SeparatorText(u8"\u041e\u0431\u044a\u0435\u0434\u0438\u043d\u0435\u043d\u0438\u0435 \u043e\u0442\u0440\u0438\u0441\u043e\u0432\u043e\u043a");
    ImGui::Text(u8"\u041b\u043e\u0433\u0438\u0447\u0435\u0441\u043a\u0438\u0445 \u043e\u0442\u0440\u0438\u0441\u043e\u0432\u043e\u043a: %llu  \u0444\u0438\u0437\u0438\u0447\u0435\u0441\u043a\u0438\u0445 \u043f\u0430\u043a\u0435\u0442\u043e\u0432: %llu",
                static_cast<unsigned long long>(resources.native_logical_draws),
                static_cast<unsigned long long>(resources.native_physical_batches));
    ImGui::Text(u8"\u0421 \u043f\u0435\u0440\u0435\u0441\u0442\u0430\u043d\u043e\u0432\u043a\u043e\u0439: %llu  \u0441 \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u0435\u043c \u043f\u043e\u0440\u044f\u0434\u043a\u0430: %llu",
                static_cast<unsigned long long>(resources.native_reorderable_draws),
                static_cast<unsigned long long>(resources.native_ordered_draws));
    ImGui::Text(u8"\u041d\u0435\u043f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u044b\u0445: %llu  \u0441 \u0432\u044b\u0440\u0435\u0437\u0430\u043c\u0438: %llu  \u0433\u0440\u0443\u043f\u043f \u043c\u0430\u0442\u0435\u0440\u0438\u0430\u043b\u043e\u0432: %llu",
                static_cast<unsigned long long>(resources.native_opaque_draws),
                static_cast<unsigned long long>(resources.native_cutout_draws),
                static_cast<unsigned long long>(resources.native_material_buckets));
    ImGui::Text(u8"\u0420\u0430\u0441\u0447\u0451\u0442\u043e\u0432 \u0432\u0438\u0434\u0438\u043c\u043e\u0441\u0442\u0438: %llu  \u043e\u0434\u0438\u043d\u043e\u0447\u043d\u044b\u0445 \u043f\u0430\u043a\u0435\u0442\u043e\u0432: %llu",
                static_cast<unsigned long long>(resources.native_compute_dispatches),
                static_cast<unsigned long long>(resources.native_one_draw_batches));
    ImGui::Text(u8"\u0421\u0430\u043c\u044b\u0439 \u0431\u043e\u043b\u044c\u0448\u043e\u0439 \u043f\u0430\u043a\u0435\u0442: %llu \u043e\u0442\u0440\u0438\u0441\u043e\u0432\u043e\u043a",
                static_cast<unsigned long long>(resources.native_max_batch_draws));
    ImGui::Text(u8"\u041c\u0430\u0441\u0441\u0438\u0432\u044b \u0442\u0435\u043a\u0441\u0442\u0443\u0440 \u0437\u0435\u043c\u043b\u0438: %llu / %llu \u0443\u0441\u043f\u0435\u0448\u043d\u043e",
                static_cast<unsigned long long>(resources.native_texture_array_successes),
                static_cast<unsigned long long>(resources.native_texture_array_attempts));
    ImGui::Text(u8"\u041e\u0442\u043a\u0430\u0437\u044b \u043c\u0430\u0441\u0441\u0438\u0432\u043e\u0432: \u043d\u0435\u0442 \u0434\u0430\u043d\u043d\u044b\u0445/\u0444\u043e\u0440\u043c\u0430\u0442/\u0441\u0442\u0440\u0443\u043a\u0442\u0443\u0440\u0430/\u043f\u0430\u043c\u044f\u0442\u044c: %llu / %llu / %llu / %llu",
                static_cast<unsigned long long>(resources.native_texture_array_fallback_unavailable),
                static_cast<unsigned long long>(resources.native_texture_array_fallback_format),
                static_cast<unsigned long long>(resources.native_texture_array_invalid_layout),
                static_cast<unsigned long long>(resources.native_texture_array_allocation_failures));
    ImGui::Text(u8"\u041c\u0430\u0441\u0441\u0438\u0432\u044b: \u043a\u043b\u0430\u0441\u0441\u044b/\u0441\u0442\u0440\u0430\u043d\u0438\u0446\u044b/\u0441\u043b\u043e\u0438: %llu / %llu / %llu",
                static_cast<unsigned long long>(resources.native_texture_array_classes),
                static_cast<unsigned long long>(resources.native_texture_array_pages),
                static_cast<unsigned long long>(resources.native_texture_array_slices));
    ImGui::Text(u8"\u041f\u043e\u0440\u044f\u0434\u043e\u043a \u043b\u0430\u043d\u0434\u0448\u0430\u0444\u0442\u0430: \u0438\u0437\u043c\u0435\u043d\u0451\u043d/\u0441\u043e\u0445\u0440\u0430\u043d\u0451\u043d: %llu / %llu",
                static_cast<unsigned long long>(resources.native_terrain_reorderable_contracts),
                static_cast<unsigned long long>(resources.native_terrain_preserve_contracts));
    ImGui::Text(u8"\u041b\u0430\u043d\u0434\u0448\u0430\u0444\u0442, \u0441\u0442\u0430\u0434\u0438\u044f 0: \u0441 \u0442\u0435\u043a\u0441\u0442\u0443\u0440\u043e\u0439/\u0431\u0435\u0437: %llu / %llu",
                static_cast<unsigned long long>(resources.native_terrain_textured_draws),
                static_cast<unsigned long long>(resources.native_terrain_untextured_draws));
    ImGui::Text(u8"\u041f\u0435\u0440\u0435\u0434\u0430\u0447\u0430 \u0442\u043e\u043f\u043e\u043b\u043e\u0433\u0438\u0438: %llu / %llu \u0432\u0435\u0440\u0448\u0438\u043d  \u0442\u043e\u043b\u044c\u043a\u043e \u0446\u0432\u0435\u0442\u0430: %llu / %llu \u0432\u0435\u0440\u0448\u0438\u043d",
                static_cast<unsigned long long>(resources.native_terrain_topology_uploads),
                static_cast<unsigned long long>(resources.native_terrain_topology_upload_vertices),
                static_cast<unsigned long long>(resources.native_terrain_color_only_uploads),
                static_cast<unsigned long long>(resources.native_terrain_color_only_upload_vertices));

    if (bridge) {
        ImGui::SeparatorText(u8"CPU \u0434\u0432\u0438\u0436\u043a\u0430 \u0438 API");
        ImGui::Text(u8"\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b \u043d\u0430\u0447\u0430\u043b\u0430 \u0441\u0446\u0435\u043d \u0434\u0432\u0438\u0436\u043a\u0430: %.3f \u043c\u0441", bridge->begin_interval_ms);
        ImGui::Text(u8"\u0418\u0437\u043c\u0435\u0440\u0435\u043d\u043e CPU \u043a\u0430\u0434\u0440\u0430: %.3f \u043c\u0441  \u0432\u043d\u0435 \u0442\u043e\u0447\u0435\u043a \u0432\u0445\u043e\u0434\u0430: %.3f \u043c\u0441",
                    bridge->frame_cpu_ms, bridge->outside_entry_cpu_ms);
        ImGui::Text(u8"\u041e\u0442\u0440\u0438\u0441\u043e\u0432\u043a\u0430 \u043c\u0438\u0440\u0430: %.3f \u043c\u0441 / %llu  \u043d\u0435 \u0443\u0447\u0442\u0435\u043d\u043e: %.3f \u043c\u0441",
                    bridge->world_draw_cpu_ms,
                    static_cast<unsigned long long>(bridge->world_draw_calls),
                    bridge->world_unattributed_cpu_ms);
        ImGui::Text(u8"3D-\u0441\u0446\u0435\u043d\u0430: %.3f \u043c\u0441 / %llu  \u043d\u0435 \u0443\u0447\u0442\u0435\u043d\u043e: %.3f \u043c\u0441  \u0432\u043d\u0435 3D: %.3f \u043c\u0441",
                    bridge->scene_3d_cpu_ms,
                    static_cast<unsigned long long>(bridge->scene_3d_calls),
                    bridge->scene_3d_unattributed_cpu_ms,
                    bridge->outside_scene_3d_cpu_ms);
        ImGui::Text(u8"3D-\u043e\u0431\u044a\u0435\u043a\u0442\u044b: %.3f \u043c\u0441 / %llu  \u043e\u0441\u0442\u0430\u0442\u043e\u043a \u043f\u043e \u043e\u0431\u044a\u0435\u043a\u0442\u0430\u043c: %.3f \u043c\u0441",
                    bridge->object_draw_cpu_ms,
                    static_cast<unsigned long long>(bridge->object_draw_calls),
                    bridge->object_draw_unattributed_cpu_ms);
        ImGui::Text(u8"\u041f\u0440\u043e\u0441\u0442\u0430\u044f \u0433\u0435\u043e\u043c\u0435\u0442\u0440\u0438\u044f: %.3f \u043c\u0441 / %llu \u0432\u044b\u0437\u043e\u0432\u043e\u0432", bridge->simple_cpu_ms,
                    static_cast<unsigned long long>(bridge->simple_calls));
        ImGui::Text(u8"\u041f\u0440\u0438\u0451\u043c \u043b\u0430\u043d\u0434\u0448\u0430\u0444\u0442\u0430: %.3f \u043c\u0441 / %llu \u0432\u044b\u0437\u043e\u0432\u043e\u0432", bridge->terrain_cpu_ms,
                    static_cast<unsigned long long>(bridge->terrain_calls));
        ImGui::Text(u8"\u0414\u0438\u0430\u043f\u0430\u0437\u043e\u043d\u044b \u043b\u0430\u043d\u0434\u0448\u0430\u0444\u0442\u0430: %.3f \u043c\u0441 / %llu \u0432\u044b\u0437\u043e\u0432\u043e\u0432  \u0437\u0430\u0445\u0432\u0430\u0442: %.3f \u043c\u0441  \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0430: %.3f \u043c\u0441",
                    bridge->terrain_outer_cpu_ms,
                    static_cast<unsigned long long>(bridge->terrain_outer_calls),
                    bridge->terrain_capture_cpu_ms, bridge->terrain_submit_cpu_ms);
        ImGui::Text(u8"\u041b\u0430\u043d\u0434\u0448\u0430\u0444\u0442: \u043f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043a\u0430/\u043f\u043e\u0432\u0442\u043e\u0440/\u043f\u0440\u043e\u043f\u0443\u0441\u043a/\u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b: %llu / %llu / %llu / %llu",
                    static_cast<unsigned long long>(bridge->terrain_owned_frame_preparations),
                    static_cast<unsigned long long>(bridge->terrain_owned_transparent_reuses),
                    static_cast<unsigned long long>(bridge->terrain_owned_omitted_ranges),
                    static_cast<unsigned long long>(
                        bridge->terrain_original_render_front_end_calls));
        ImGui::Text(u8"\u041f\u0443\u0431\u043b\u0438\u043a\u0430\u0446\u0438\u044f \u043b\u0430\u043d\u0434\u0448\u0430\u0444\u0442\u0430: %llu \u0441\u0435\u043a\u0442\u043e\u0440\u043e\u0432  \u0434\u0438\u043d\u0430\u043c\u0438\u0447\u0435\u0441\u043a\u0438\u0435 \u0446\u0432\u0435\u0442\u0430: %llu \u0432\u0435\u0440\u0448\u0438\u043d",
                    static_cast<unsigned long long>(bridge->terrain_game_publication_sectors),
                    static_cast<unsigned long long>(
                        bridge->terrain_dynamic_color_vertices_copied));
        ImGui::Text(u8"\u041a\u043e\u043f\u0438\u0438 \u0441\u0442\u0440\u0443\u043a\u0442\u0443\u0440\u044b \u0437\u0435\u043c\u043b\u0438: %llu \u0432\u0435\u0440\u0448\u0438\u043d  \u0438\u0441\u0445\u043e\u0434\u043d\u0430\u044f \u0442\u043e\u043f\u043e\u043b\u043e\u0433\u0438\u044f: %llu / %llu \u0432\u0435\u0440\u0448\u0438\u043d",
                    static_cast<unsigned long long>(
                        bridge->terrain_structural_vertices_copied),
                    static_cast<unsigned long long>(bridge->terrain_source_topology_decodes),
                    static_cast<unsigned long long>(bridge->terrain_source_topology_vertices));
        ImGui::Text(u8"\u0414\u0435\u043a\u043e\u0434\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435 \u0446\u0432\u0435\u0442\u043e\u0432 \u0437\u0435\u043c\u043b\u0438: %llu / %llu \u0432\u0435\u0440\u0448\u0438\u043d",
                    static_cast<unsigned long long>(bridge->terrain_source_color_decodes),
                    static_cast<unsigned long long>(bridge->terrain_source_color_vertices));
        ImGui::Text(u8"\u041b\u0430\u043d\u0434\u0448\u0430\u0444\u0442 \u043d\u0430 GPU: %llu  \u043f\u0443\u0442\u0438 CPU: %llu  \u043e\u0448\u0438\u0431\u043a\u0438 \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0438: %llu",
                    static_cast<unsigned long long>(bridge->terrain_gpu_owned_calls),
                    static_cast<unsigned long long>(bridge->terrain_cpu_fallback_calls),
                    static_cast<unsigned long long>(bridge->terrain_gpu_submit_failures));
        ImGui::Text(u8"\u0421\u0435\u043a\u0442\u043e\u0440\u044b: \u043a\u0430\u043d\u0434\u0438\u0434\u0430\u0442\u044b/\u0432\u0438\u0434\u0438\u043c\u044b\u0435/\u043e\u0442\u0441\u0435\u0447\u0451\u043d\u043d\u044b\u0435: %llu / %llu / %llu",
                    static_cast<unsigned long long>(bridge->terrain_candidate_sectors),
                    static_cast<unsigned long long>(bridge->terrain_visible_sectors),
                    static_cast<unsigned long long>(bridge->terrain_frustum_rejected_sectors));
        ImGui::Text(u8"\u0410\u043d\u0438\u043c\u0430\u0446\u0438\u044f \u0437\u0435\u043c\u043b\u0438 GPU/CPU: %llu / %llu  \u0432\u043e\u043b\u043d\u043e\u0432\u044b\u0445 \u0441\u0435\u043a\u0442\u043e\u0440\u043e\u0432: %llu  \u0444\u0430\u0437 \u0442\u0430\u0439\u043b\u043e\u0432: %llu",
                    static_cast<unsigned long long>(bridge->terrain_animation_owned_updates),
                    static_cast<unsigned long long>(bridge->terrain_animation_fallback_updates),
                    static_cast<unsigned long long>(bridge->terrain_wave_sectors),
                    static_cast<unsigned long long>(bridge->terrain_animated_tile_updates));
        ImGui::Text(u8"\u0420\u0430\u0437\u043c\u0435\u0449\u0451\u043d\u043d\u044b\u0435 \u043c\u043e\u0434\u0435\u043b\u0438: %.3f \u043c\u0441 / %llu \u0432\u044b\u0437\u043e\u0432\u043e\u0432", bridge->figure_placed_cpu_ms,
                    static_cast<unsigned long long>(bridge->figure_placed_calls));
        ImGui::Text(u8"\u041e\u0441\u0432\u0435\u0449\u0451\u043d\u043d\u044b\u0435 \u043c\u043e\u0434\u0435\u043b\u0438: %.3f \u043c\u0441 / %llu \u0432\u044b\u0437\u043e\u0432\u043e\u0432", bridge->figure_lit_cpu_ms,
                    static_cast<unsigned long long>(bridge->figure_lit_calls));
        ImGui::Text(u8"\u041f\u0440\u0438\u0451\u043c \u043e\u0441\u0432\u0435\u0449\u0435\u043d\u0438\u044f: \u043c\u043e\u0440\u0444\u0438\u043d\u0433/\u0436\u0451\u0441\u0442\u043a\u0438\u0435 \u0438\u043b\u0438 \u043f\u0440\u043e\u0447\u0438\u0435: %.3f/%0.3f \u043c\u0441  %llu/%llu \u0432\u044b\u0437\u043e\u0432\u043e\u0432",
                    bridge->figure_lit_morphed_cpu_ms,
                    bridge->figure_lit_rigid_cpu_ms,
                    static_cast<unsigned long long>(bridge->figure_lit_morphed_calls),
                    static_cast<unsigned long long>(bridge->figure_lit_rigid_calls));
        ImGui::Text(u8"\u041c\u043e\u0434\u0435\u043b\u0438 \u043d\u0430 GPU: %llu  \u043f\u0443\u0442\u0438 CPU: %llu",
                    static_cast<unsigned long long>(bridge->figure_gpu_owned_calls),
                    static_cast<unsigned long long>(bridge->figure_cpu_fallback_calls));
        ImGui::Text(u8"\u041e\u0448\u0438\u0431\u043a\u0438 \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0438 GPU: %llu  \u043e\u0442\u0441\u0435\u0447\u0435\u043d\u043e \u0437\u0430\u0440\u0430\u043d\u0435\u0435: %llu",
                    static_cast<unsigned long long>(bridge->figure_gpu_submit_failures),
                    static_cast<unsigned long long>(bridge->figure_gpu_culled_calls));
        for (std::size_t index = 1; index < bridge->figure_fallback_reasons.size(); ++index) {
            const auto fallback_count = bridge->figure_fallback_reasons[index];
            if (!fallback_count) continue;
            ImGui::Text(u8"\u0410\u043b\u044c\u0442\u0435\u0440\u043d\u0430\u0442\u0438\u0432\u043d\u044b\u0439 \u043f\u0443\u0442\u044c %-14s %llu",
                        figure_fallback_reason_name(static_cast<FigureFallbackReason>(index)),
                        static_cast<unsigned long long>(fallback_count));
        }
    }
}

bool interface_option(const RendererOption& option) {
    if(option.group=="Interface" || option.group=="HUD layout" || option.group=="Combat UI")return true;
    std::size_t count=0;const auto* settings=player_graphics_settings(count);
    for(std::size_t i=0;i<count;++i)
        if(option.key==settings[i].key && std::strcmp(settings[i].category,u8"\u0418\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441")==0)return true;
    return false;
}
void reset_interface(RendererConfig& config) {
    for(const auto& option:config.options()) {
        if(!interface_option(option))continue;
        switch(option.type) {
        case RendererOptionType::boolean:config.set_boolean(option.key.c_str(),option.boolean_default);break;
        case RendererOptionType::integer:config.set_integer(option.key.c_str(),option.integer_default);break;
        case RendererOptionType::floating_point:config.set_floating_point(option.key.c_str(),option.floating_default);break;
        case RendererOptionType::text:config.set_text(option.key.c_str(),"");break;
        }
    }
}
// Options grouped "Diagnostics" (debug views such as the critter showcase)
// get their own tab; the Graphics tab lists every other group.
constexpr const char* kDiagnosticsGroup = "Diagnostics";
void draw_graphics_tab(RendererConfig& config, ImGuiTextFilter& filter, bool diagnostics = false,bool ui_only = false) {
    filter.Draw(u8"\u041f\u043e\u0438\u0441\u043a \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a", -1.f);
    if (config.options().empty()) {
        ImGui::TextWrapped(u8"\u0412 \u044d\u0442\u043e\u0439 \u0441\u0431\u043e\u0440\u043a\u0435 \u0440\u0435\u043d\u0434\u0435\u0440\u0430 \u043d\u0435\u0442 \u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b\u0445 \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u043e\u0432 \u0433\u0440\u0430\u0444\u0438\u043a\u0438. "
                           u8"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u043f\u043e\u044f\u0432\u043b\u044f\u044e\u0442\u0441\u044f \u043f\u043e\u0441\u043b\u0435 \u043f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f \u0441\u043e\u043e\u0442\u0432\u0435\u0442\u0441\u0442\u0432\u0443\u044e\u0449\u0438\u0445 \u0444\u0443\u043d\u043a\u0446\u0438\u0439.");
        return;
    }
    std::string current_group;
    for (const auto& option : config.options()) {
        if (option.group == "Combat UI") continue;
        if(option.group=="HUD layout" || (option.group==kDiagnosticsGroup)!=diagnostics ||
           (!diagnostics && interface_option(option)!=ui_only))continue;
        const std::string searchable = option.label + " " + option.key + " " + option.group + " " + option.description;
        if (!filter.PassFilter(searchable.c_str())) continue;
        if (option.group != current_group) {
            current_group = option.group;
            ImGui::SeparatorText(renderer_ui_text_ru(current_group.c_str()));
        }
        ImGui::PushID(option.key.c_str());
        if (ImGui::BeginTable("##setting", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", option.label.c_str());
        help_marker(option.description.c_str());
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char* note = blocked_option_note(config, option.key.c_str());
        ImGui::BeginDisabled(note != nullptr);
        switch (option.type) {
        case RendererOptionType::text: break;
        case RendererOptionType::boolean: {
            bool value = option.boolean_value;
            if (ImGui::Checkbox("##value", &value)) config.set_boolean(option.key.c_str(), value);
            break;
        }
        case RendererOptionType::integer: {
            int value = option.integer_value;
            if (option.key == "camp_ui_layout") {
                const char* choices[] = {u8"\u0418\u0441\u0445\u043e\u0434\u043d\u044b\u0439 (\u0434\u043b\u044f \u0441\u0440\u0430\u0432\u043d\u0435\u043d\u0438\u044f)", u8"\u0428\u0438\u0440\u043e\u043a\u0430\u044f \u0441\u0435\u0442\u043a\u0430"};
                if (ImGui::Combo("##value", &value, choices, 2))
                    config.set_integer(option.key.c_str(), value);
            } else if(option.key=="portrait_direction") {
                const char* choices[]={u8"\u041f\u043e \u0446\u0435\u043d\u0442\u0440\u0443 (\u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b)",u8"\u0421\u0432\u0435\u0440\u0445\u0443 \u0432\u043d\u0438\u0437",u8"\u0421\u043d\u0438\u0437\u0443 \u0432\u0432\u0435\u0440\u0445",u8"\u0421\u043b\u0435\u0432\u0430 \u043d\u0430\u043f\u0440\u0430\u0432\u043e",u8"\u0421\u043f\u0440\u0430\u0432\u0430 \u043d\u0430\u043b\u0435\u0432\u043e"};
                if(ImGui::Combo("##value",&value,choices,5))config.set_integer(option.key.c_str(),value);
            } else if(option.key=="spell_bar_direction" || option.key=="ability_bar_direction" || option.key=="weapon_switcher_direction" || option.key=="quick_items_direction") {
                const char* choices[]={u8"\u0421\u043d\u0438\u0437\u0443 \u0432\u0432\u0435\u0440\u0445",u8"\u0421\u0432\u0435\u0440\u0445\u0443 \u0432\u043d\u0438\u0437",u8"\u0421\u043b\u0435\u0432\u0430 \u043d\u0430\u043f\u0440\u0430\u0432\u043e",u8"\u0421\u043f\u0440\u0430\u0432\u0430 \u043d\u0430\u043b\u0435\u0432\u043e"};
                if(ImGui::Combo("##value",&value,choices,4))config.set_integer(option.key.c_str(),value);
            } else if (option.key == "anisotropy") {
                const char* choices[] = {"1", "2", "4", "8", "16"};
                if (ImGui::Combo("##value", &value, choices, 5))
                    config.set_integer(option.key.c_str(), value);
            } else if (option.key == "shadow_cascade_quality") {
                const char* choices[] = {u8"\u041d\u0438\u0437\u043a\u043e\u0435 (1024 x 1024)", u8"\u0421\u0440\u0435\u0434\u043d\u0435\u0435 (2048 x 2048)",
                                         u8"\u0412\u044b\u0441\u043e\u043a\u043e\u0435 (4096 x 4096)"};
                if (ImGui::Combo("##value", &value, choices, 3))
                    config.set_integer(option.key.c_str(), value);
            } else if (option.key == "point_shadow_quality") {
                const char* choices[] = {u8"\u041d\u0438\u0437\u043a\u043e\u0435 (128 x 128)", u8"\u0421\u0440\u0435\u0434\u043d\u0435\u0435 (256 x 256)",
                                         u8"\u0412\u044b\u0441\u043e\u043a\u043e\u0435 (512 x 512)"};
                if (ImGui::Combo("##value", &value, choices, 3))
                    config.set_integer(option.key.c_str(), value);
            } else if (ImGui::SliderInt("##value", &value,
                                       option.integer_minimum, option.integer_maximum)) {
                config.set_integer(option.key.c_str(), value);
            }
            break;
        }
        case RendererOptionType::floating_point: {
            float value = option.floating_value;
            const char* format = option.floating_step < 0.1f ? "%.3f" : "%.2f";
            if (ImGui::SliderFloat("##value", &value, option.floating_minimum,
                                   option.floating_maximum, format))
                config.set_floating_point(option.key.c_str(), value);
            break;
        }
        }
        ImGui::EndDisabled();
        if (note) ImGui::TextWrapped("%s", note);
        if (option.restart_required) {
            ImGui::SameLine();
            ImGui::TextDisabled(u8"\u043d\u0443\u0436\u0435\u043d \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u043a");
        }
        ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

void close_settings(DebugOverlay::Implementation& state,RendererConfig& config,bool keep);

void draw_hud_layout_control(DebugOverlay::Implementation& state,RendererConfig& config) {
    HudPanelBounds b;
    const bool available=game_hud_editor_available() && widescreen_hud_panel_bounds(HudPanel::messages,b);
    ImGui::BeginDisabled(!available && !game_hud_editing());
    if(ImGui::Button(game_hud_editing() ? u8"\u0417\u0430\u043a\u0440\u0435\u043f\u0438\u0442\u044c \u043f\u0430\u043d\u0435\u043b\u0438" : u8"\u041f\u0435\u0440\u0435\u043c\u0435\u0441\u0442\u0438\u0442\u044c \u043f\u0430\u043d\u0435\u043b\u0438")) {
        if(game_hud_editing()) {set_game_hud_editing(false);state.pending_save=true;}
        else {
            if(state.settings_open)close_settings(state,config,true);
            config.set_overlay_mode(DebugOverlayMode::hidden);
            set_game_hud_editing(true);
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(u8"\u041f\u0435\u0440\u0435\u0442\u0430\u0441\u043a\u0438\u0432\u0430\u0439\u0442\u0435 \u0441\u0432\u043e\u0431\u043e\u0434\u043d\u044b\u0435 \u043f\u0430\u043d\u0435\u043b\u0438 \u043c\u044b\u0448\u044c\u044e. \u0422\u044f\u043d\u0438\u0442\u0435 \u0437\u0430 \u0443\u0433\u043e\u043b \u0434\u043b\u044f \u0438\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u0440\u0430\u0437\u043c\u0435\u0440\u0430. F10 \u0441\u043d\u043e\u0432\u0430 \u043e\u0442\u043a\u0440\u044b\u0432\u0430\u0435\u0442 \u043c\u0435\u043d\u044e, \u0447\u0442\u043e\u0431\u044b \u0437\u0430\u043a\u0440\u0435\u043f\u0438\u0442\u044c \u0438 \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0440\u0430\u0441\u043f\u043e\u043b\u043e\u0436\u0435\u043d\u0438\u0435.");
}

void hud_bounds_to_output(HudPanelBounds& b) {
    const auto& native=widescreen_layout();const auto output=ImGui::GetIO().DisplaySize;
    const float sx=output.x/native.width,sy=output.y/native.height;
    b.left*=sx;b.top*=sy;b.width*=sx;b.height*=sy;b.native_width*=sx;b.native_height*=sy;
}
float snap_axis(float value,float size,float screen,unsigned moving,bool horizontal,float threshold) {
    float best=value,distance=threshold;
    const auto consider=[&](float target) {const float d=std::fabs(value-target);if(d<distance){best=target;distance=d;}};
    consider(0.f);consider(screen-size);
    for(unsigned i=0;i<kHudPanels.size();++i) {
        if(i==moving)continue;
        HudPanelBounds b;if(!widescreen_hud_panel_bounds(HudPanel(i),b))continue;
        HudPanelBounds header;
        if(widescreen_hud_header_bounds(HudPanel(i),header)) {
            const float right=(std::max)(b.left+b.width,header.left+header.width);
            const float bottom=(std::max)(b.top+b.height,header.top+header.height);
            b.left=(std::min)(b.left,header.left);b.top=(std::min)(b.top,header.top);
            b.width=right-b.left;b.height=bottom-b.top;
        }
        hud_bounds_to_output(b);
        const float first=horizontal?b.left:b.top,last=first+(horizontal?b.width:b.height);
        consider(first);consider(last);consider(first-size);consider(last-size);
    }
    return best;
}
void reset_hud_panel(RendererConfig& config,unsigned panel) {
    const std::string prefix=std::string(kHudPanels[panel].key)+"_";
    for(const auto& option:config.options()) {
        if(option.key.compare(0,prefix.size(),prefix)!=0 &&
           !(HudPanel(panel)==HudPanel::portraits && option.key=="portrait_direction"))continue;
        if(option.type==RendererOptionType::boolean)config.set_boolean(option.key.c_str(),option.boolean_default);
        else if(option.type==RendererOptionType::integer)config.set_integer(option.key.c_str(),option.integer_default);
        else if(option.type==RendererOptionType::floating_point)config.set_floating_point(option.key.c_str(),option.floating_default);
    }
}
void hud_scale_control(RendererConfig& config,const char* key,const char* label) {
    const auto* option=config.find(key);if(!option)return;
    if(option->type==RendererOptionType::integer) {
        int value=option->integer_value;
        if(ImGui::SliderInt(label,&value,option->integer_minimum,option->integer_maximum,"%d%%"))
            config.set_integer(key,value);
    } else if(option->type==RendererOptionType::floating_point) {
        float value=option->floating_value;
        if(ImGui::SliderFloat(label,&value,option->floating_minimum,option->floating_maximum,"%.0f%%"))
            config.set_floating_point(key,value);
    }
}
void hud_panel_options(RendererConfig& config,unsigned panel,const HudPanelBounds& bounds) {
    ImGui::TextUnformatted(renderer_ui_text_ru(kHudPanels[panel].label));
    ImGui::Separator();
    ImGui::Text(u8"\u041f\u043e\u043b\u043e\u0436\u0435\u043d\u0438\u0435: X %.0f, Y %.0f \u043f\u0438\u043a\u0441.",bounds.left,bounds.top);
    ImGui::Text(u8"\u0420\u0430\u0437\u043c\u0435\u0440: %.0f x %.0f \u043f\u0438\u043a\u0441.",bounds.width,bounds.height);
    if(HudPanel(panel)==HudPanel::messages) {
        hud_scale_control(config,"message_panel_text_scale",u8"\u041c\u0430\u0441\u0448\u0442\u0430\u0431 \u0442\u0435\u043a\u0441\u0442\u0430");
    } else {
        const auto key=std::string(kHudPanels[panel].key)+"_scale";
        hud_scale_control(config,key.c_str(),u8"\u041c\u0430\u0441\u0448\u0442\u0430\u0431 \u043f\u0430\u043d\u0435\u043b\u0438");
    }
    const std::string fade_prefix=kHudPanels[panel].key;
    if(const auto* fade=config.find((fade_prefix+"_auto_fade").c_str())){
        bool enabled=fade->boolean_value;
        if(ImGui::Checkbox(u8"\u0410\u0432\u0442\u043e\u0441\u043a\u0440\u044b\u0442\u0438\u0435",&enabled))config.set_boolean(fade->key.c_str(),enabled);
        hud_scale_control(config,(fade_prefix+"_fade_transparency").c_str(),u8"\u041f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u043e\u0441\u0442\u044c \u0441\u043a\u0440\u044b\u0442\u043e\u0439 \u043f\u0430\u043d\u0435\u043b\u0438");
        ImGui::TextDisabled("0%% = solid; 100%% = hidden");
    }
    if(panel>=unsigned(HudPanel::stance))ImGui::TextDisabled(u8"\u0417\u0430\u043a\u0440\u0435\u043f\u043b\u0435\u043d\u0438\u0435 \u0432 \u0443\u0433\u043b\u0443 \u044d\u043a\u0440\u0430\u043d\u0430");
    const char* direction=nullptr;
    if(HudPanel(panel)==HudPanel::spells)direction="spell_bar_direction";
    else if(HudPanel(panel)==HudPanel::abilities)direction="ability_bar_direction";
    else if(HudPanel(panel)==HudPanel::portraits)direction="portrait_direction";
    else if(HudPanel(panel)==HudPanel::weapons)direction="weapon_switcher_direction";
    else if(HudPanel(panel)==HudPanel::items)direction="quick_items_direction";
    if(direction) {
        const auto* option=config.find(direction);
        if(option) {
            int value=option->integer_value;
            const char* bars[]={u8"\u0421\u043d\u0438\u0437\u0443 \u0432\u0432\u0435\u0440\u0445",u8"\u0421\u0432\u0435\u0440\u0445\u0443 \u0432\u043d\u0438\u0437",u8"\u0421\u043b\u0435\u0432\u0430 \u043d\u0430\u043f\u0440\u0430\u0432\u043e",u8"\u0421\u043f\u0440\u0430\u0432\u0430 \u043d\u0430\u043b\u0435\u0432\u043e"};
            const char* portraits[]={u8"\u041f\u043e \u0446\u0435\u043d\u0442\u0440\u0443 (\u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b)",u8"\u0421\u0432\u0435\u0440\u0445\u0443 \u0432\u043d\u0438\u0437",u8"\u0421\u043d\u0438\u0437\u0443 \u0432\u0432\u0435\u0440\u0445",u8"\u0421\u043b\u0435\u0432\u0430 \u043d\u0430\u043f\u0440\u0430\u0432\u043e",u8"\u0421\u043f\u0440\u0430\u0432\u0430 \u043d\u0430\u043b\u0435\u0432\u043e"};
            const bool faces=HudPanel(panel)==HudPanel::portraits;
            if(ImGui::Combo(u8"\u0420\u0430\u0441\u043f\u043e\u043b\u043e\u0436\u0435\u043d\u0438\u0435 \u0437\u043d\u0430\u0447\u043a\u043e\u0432",&value,faces?portraits:bars,faces?5:4))
                config.set_integer(direction,value);
        }
    }
    if(direction) {
        const std::string prefix=kHudPanels[panel].key;
        const auto* padding=config.find((prefix+"_padding").c_str());
        if(padding) {int value=padding->integer_value;if(ImGui::SliderInt(u8"\u041e\u0442\u0441\u0442\u0443\u043f (\u043f\u0438\u043a\u0441.)",&value,padding->integer_minimum,padding->integer_maximum))config.set_integer(padding->key.c_str(),value);}
        const auto* preview=config.find((prefix+"_preview").c_str());
        if(preview) {bool value=preview->boolean_value;if(ImGui::Checkbox(u8"\u041f\u0440\u0435\u0434\u043f\u0440\u043e\u0441\u043c\u043e\u0442\u0440 \u044f\u0447\u0435\u0435\u043a",&value))config.set_boolean(preview->key.c_str(),value);}
        const auto* count=config.find((prefix+"_preview_count").c_str());
        if(count) {int value=count->integer_value;if(ImGui::SliderInt(u8"\u041a\u043e\u043b\u0438\u0447\u0435\u0441\u0442\u0432\u043e \u044f\u0447\u0435\u0435\u043a \u043f\u0440\u0435\u0434\u043f\u0440\u043e\u0441\u043c\u043e\u0442\u0440\u0430",&value,count->integer_minimum,count->integer_maximum))config.set_integer(count->key.c_str(),value);}
        const auto* wrap=config.find((prefix+"_slots_per_column").c_str());
        if(wrap) {int value=wrap->integer_value;
            const bool rows=(direction && config.find(direction) && config.find(direction)->integer_value>=2);
            if(ImGui::SliderInt(rows?u8"\u042f\u0447\u0435\u0435\u043a \u0432 \u0441\u0442\u0440\u043e\u043a\u0435":u8"\u042f\u0447\u0435\u0435\u043a \u0432 \u0441\u0442\u043e\u043b\u0431\u0446\u0435",&value,wrap->integer_minimum,wrap->integer_maximum,value?"%d":u8"0 (\u043e\u0434\u0438\u043d \u0440\u044f\u0434)"))config.set_integer(wrap->key.c_str(),value);}
    }
    if(HudPanel(panel)==HudPanel::minimap || HudPanel(panel)==HudPanel::character_info || HudPanel(panel)==HudPanel::messages) {
        const std::string prefix=kHudPanels[panel].key;
        hud_scale_control(config,(prefix+"_buttons_scale").c_str(),u8"\u041c\u0430\u0441\u0448\u0442\u0430\u0431 \u043a\u043d\u043e\u043f\u043e\u043a");
        const auto* gap=config.find((prefix+"_border_padding").c_str());
        if(gap){int value=gap->integer_value;if(ImGui::SliderInt(u8"\u041e\u0442\u0441\u0442\u0443\u043f \u0440\u0430\u043c\u043a\u0438 (\u043f\u0438\u043a\u0441.)",&value,gap->integer_minimum,gap->integer_maximum))config.set_integer(gap->key.c_str(),value);}
        const auto* option=config.find((prefix+"_buttons_side").c_str());
        if(option) {int value=option->integer_value;
            const char* sides[]={u8"\u0410\u0432\u0442\u043e\u043c\u0430\u0442\u0438\u0447\u0435\u0441\u043a\u0438",u8"\u0428\u0442\u0430\u0442\u043d\u0430\u044f \u0441\u0442\u043e\u0440\u043e\u043d\u0430",u8"\u0421\u043b\u0435\u0432\u0430",u8"\u0421\u043f\u0440\u0430\u0432\u0430",u8"\u0421\u0432\u0435\u0440\u0445\u0443",u8"\u0421\u043d\u0438\u0437\u0443"};
            if(ImGui::Combo(u8"\u0420\u0430\u0441\u043f\u043e\u043b\u043e\u0436\u0435\u043d\u0438\u0435 \u043a\u043d\u043e\u043f\u043e\u043a",&value,sides,6))config.set_integer(option->key.c_str(),value);
        }
    }
    if(ImGui::Button(u8"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u044d\u0442\u0443 \u043f\u0430\u043d\u0435\u043b\u044c"))reset_hud_panel(config,panel);
}
void draw_hud_editor(DebugOverlay::Implementation& state,RendererConfig& config) {
    if(!game_hud_editing() || state.settings_open || config.overlay_mode()==DebugOverlayMode::full)return;
    auto layout=widescreen_layout();
    const auto output=ImGui::GetIO().DisplaySize;
    const float sx=output.x/layout.width,sy=output.y/layout.height;
    layout.width=output.x;layout.height=output.y;layout.unit*=sy;
    const float grip=22.f*state.ui_scale,grid=24.f*state.ui_scale;
    const bool snap_x=ImGui::GetIO().KeyCtrl,snap_y=ImGui::GetIO().KeyAlt;
    const auto snap=[&](float value,float size,unsigned panel,bool horizontal) {
        if(!(horizontal?snap_x:snap_y))return value;
        if(state.hud_grid)return std::round(value/grid)*grid;
        return snap_axis(value,size,horizontal?layout.width:layout.height,panel,horizontal,8.f*state.ui_scale);
    };
    bool available=false;
    for(unsigned i=0;i<kHudPanels.size();++i) {
        HudPanelBounds b;if(!widescreen_hud_panel_bounds(HudPanel(i),b))continue;
        hud_bounds_to_output(b);
        available=true;
        const ImVec2 content(b.left,b.top),size(b.width,b.height);
        ImVec2 at=content,hit_size=size;HudPanelBounds header;
        if(widescreen_hud_header_bounds(HudPanel(i),header)) {
            hud_bounds_to_output(header);
            at.x=(std::min)(at.x,header.left);at.y=(std::min)(at.y,header.top);
            hit_size.x=(std::max)(b.left+b.width,header.left+header.width)-at.x;
            hit_size.y=(std::max)(b.top+b.height,header.top+header.height)-at.y;
        }
        ImGui::SetNextWindowPos(at,ImGuiCond_Always);ImGui::SetNextWindowSize(hit_size,ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        const ImGuiWindowFlags flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|
            ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse;
        if(ImGui::Begin(renderer_ui_text_ru(kHudPanels[i].label),nullptr,flags)) {
            ImGui::SetCursorScreenPos(at);ImGui::InvisibleButton("move_or_resize",hit_size);
            const bool active=ImGui::IsItemActive();
            const bool highlighted=ImGui::IsItemHovered() || active || ImGui::IsPopupOpen(u8"\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043f\u0430\u043d\u0435\u043b\u0438");
            auto* draw=ImGui::GetWindowDrawList();
            const auto color=highlighted?IM_COL32(100,230,255,255):IM_COL32(255,215,80,255);
            if(highlighted)draw->AddRectFilled(at,ImVec2(at.x+hit_size.x,at.y+hit_size.y),IM_COL32(70,190,230,35));
            std::vector<HudPreviewSlot> previews;widescreen_hud_preview_slots(HudPanel(i),previews);
            for(const auto& preview:previews) {
                auto p=preview.bounds;hud_bounds_to_output(p);const ImVec2 first(p.left,p.top),last(p.left+p.width,p.top+p.height);
                draw->AddRectFilled(first,last,IM_COL32(150,185,210,22));
                draw->AddRect(first,last,IM_COL32(160,200,225,90),0.f,0,1.f);
                const std::string label=std::string(i==4?u8"\u041e\u0442\u0440\u044f\u0434 ":i==0?u8"\u0417\u0430\u043a\u043b\u0438\u043d\u0430\u043d\u0438\u0435 ":i==1?u8"\u0423\u043c\u0435\u043d\u0438\u0435 ":i==5?u8"\u041e\u0440\u0443\u0436\u0438\u0435 ":u8"\u041f\u0440\u0435\u0434\u043c\u0435\u0442 ")+std::to_string(preview.index+1);
                draw->AddText(ImVec2(first.x+3.f,first.y+3.f),IM_COL32(180,210,230,135),label.c_str());
            }
            // Editor guides are always one physical pixel, independent of widget scale.
            draw->AddRect(content,ImVec2(content.x+size.x,content.y+size.y),color,0.f,0,1.f);
            draw->AddText(ImVec2(at.x+5.f,at.y+5.f),color,renderer_ui_text_ru(kHudPanels[i].label));
            if(i<unsigned(HudPanel::stance))draw->AddLine(ImVec2(content.x+size.x-grip,content.y+size.y-2.f),ImVec2(content.x+size.x-2.f,content.y+size.y-grip),color,1.f);
            if(ImGui::IsItemActivated()) {
                state.panel_drag_start=content;state.panel_size_start=size;
                const auto mouse=ImGui::GetIO().MousePos;
                state.panel_resizing=mouse.x>=content.x+size.x-grip && mouse.y>=content.y+size.y-grip;
            }
            if(i<unsigned(HudPanel::stance) && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const auto delta=ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float left=state.panel_drag_start.x,top=state.panel_drag_start.y;
                float width=state.panel_size_start.x,height=state.panel_size_start.y;
                const std::string key=kHudPanels[i].key;
                if(state.panel_resizing) {
                    const float dx=snap(left+width+delta.x,0.f,i,true)-left-width;
                    const float dy=snap(top+height+delta.y,0.f,i,false)-top-height;
                    if(HudPanel(i)==HudPanel::messages) {
                        width=std::clamp(width+dx,220.f*layout.unit*layout.scale,layout.width);
                        const bool side=left<=.5f || left+width>=layout.width-.5f;
                        height=std::clamp(height+dy,(side?90.f:50.f)*layout.unit*layout.scale,layout.height);
                        config.set_floating_point("message_panel_width",width*100.f/(440.f*layout.unit*layout.scale));
                        config.set_floating_point("message_panel_height",height*100.f/(100.f*layout.unit*layout.scale));
                    } else {
                        const auto start=state.panel_size_start;
                        float ratio=(start.x*(start.x+dx)+start.y*(start.y+dy))/(start.x*start.x+start.y*start.y);
                        // A fixed-aspect widget can meet one snap axis exactly. With both
                        // held, the larger pointer movement selects the driving extent.
                        if(snap_x || snap_y)ratio=snap_x && (!snap_y || std::fabs(delta.x)>=std::fabs(delta.y))?
                            (start.x+dx)/start.x:(start.y+dy)/start.y;
                        const float header_size=(HudPanel(i)==HudPanel::minimap || HudPanel(i)==HudPanel::character_info)?30.f*layout.unit*b.width/b.native_width:0.f;
                        const float fit=(std::min)(layout.width/start.x,layout.height/(start.y+2.f*header_size));
                        const float minimum=.5f*b.native_width*layout.scale/start.x;
                        const float maximum=(std::min)(fit,3.f*b.native_width*layout.scale/start.x);
                        const float multiplier=std::clamp(ratio,(std::min)(minimum,maximum),maximum);
                        width=start.x*multiplier;height=start.y*multiplier;
                        config.set_floating_point((key+"_scale").c_str(),width*100.f/(b.native_width*layout.scale));
                    }
                } else {
                    left=snap(left+delta.x,width,i,true);
                    top=snap(top+delta.y,height,i,false);
                }
                left=std::clamp(left,0.f,(std::max)(0.f,layout.width-width));
                top=std::clamp(top,0.f,(std::max)(0.f,layout.height-height));
                if(HudPanel(i)==HudPanel::messages) {
                    config.set_floating_point("message_panel_x",(left+width*.5f)*100.f/layout.width);
                    config.set_floating_point("message_panel_y",top*100.f/layout.height);
                } else {
                    float x,y;widescreen_hud_position_percent(HudPanel(i),left/sx,top/sy,width/sx,height/sy,x,y);
                    config.set_floating_point((key+"_x").c_str(),x);
                    config.set_floating_point((key+"_y").c_str(),y);
                }
            }
            if(ImGui::BeginPopupContextItem(u8"\u041f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b \u043f\u0430\u043d\u0435\u043b\u0438")) {
                hud_panel_options(config,i,b);
                ImGui::EndPopup();
            }
        }
        ImGui::End();ImGui::PopStyleVar();
    }
    ImGui::SetNextWindowPos(ImVec2(20.f*state.ui_scale,layout.height*.4f),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290.f*state.ui_scale,0),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("HUD layout",nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_AlwaysAutoResize)) {
        hud_scale_control(config,"ui_scale",u8"\u041e\u0431\u0449\u0438\u0439 \u043c\u0430\u0441\u0448\u0442\u0430\u0431 \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430");
        const char* timing_keys[]={"hud_fade_in_duration","hud_fade_duration","hud_fade_delay"};
        const char* timing_labels[]={u8"\u0412\u0440\u0435\u043c\u044f \u043f\u043e\u044f\u0432\u043b\u0435\u043d\u0438\u044f",u8"\u0412\u0440\u0435\u043c\u044f \u0438\u0441\u0447\u0435\u0437\u043d\u043e\u0432\u0435\u043d\u0438\u044f",u8"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430 \u043f\u0435\u0440\u0435\u0434 \u0441\u043a\u0440\u044b\u0442\u0438\u0435\u043c"};
        for(unsigned i=0;i<3;++i)if(const auto* timing=config.find(timing_keys[i])) {
            int value=timing->integer_value;
            if(ImGui::SliderInt(timing_labels[i],&value,timing->integer_minimum,timing->integer_maximum,u8"%d \u043c\u0441"))
                config.set_integer(timing->key.c_str(),value);
        }
        ImGui::Checkbox(u8"\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u0441\u0435\u0442\u043a\u0443",&state.hud_grid);
        if(ImGui::Button(u8"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441"))reset_interface(config);
        ImGui::SameLine();
        if(ImGui::Button(u8"\u0417\u0430\u043a\u0440\u0435\u043f\u0438\u0442\u044c")){set_game_hud_editing(false);state.pending_save=true;}
        ImGui::TextWrapped(u8"\u041f\u0435\u0440\u0435\u0442\u0430\u0441\u043a\u0438\u0432\u0430\u0439\u0442\u0435 \u043f\u0430\u043d\u0435\u043b\u044c \u0437\u0430 \u043b\u044e\u0431\u0443\u044e \u0442\u043e\u0447\u043a\u0443; \u043c\u0435\u043d\u044f\u0439\u0442\u0435 \u0440\u0430\u0437\u043c\u0435\u0440 \u0437\u0430 \u043d\u0438\u0436\u043d\u0438\u0439 \u043f\u0440\u0430\u0432\u044b\u0439 \u0443\u0433\u043e\u043b. \u041f\u0440\u0430\u0432\u0430\u044f \u043a\u043d\u043e\u043f\u043a\u0430 \u043e\u0442\u043a\u0440\u044b\u0432\u0430\u0435\u0442 \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b, \u043a\u043e\u043e\u0440\u0434\u0438\u043d\u0430\u0442\u044b \u0438 \u0441\u0431\u0440\u043e\u0441.");
        ImGui::TextWrapped(u8"Ctrl \u0432\u043a\u043b\u044e\u0447\u0430\u0435\u0442 \u043f\u0440\u0438\u0432\u044f\u0437\u043a\u0443 \u043f\u043e \u0433\u043e\u0440\u0438\u0437\u043e\u043d\u0442\u0430\u043b\u0438, Alt \u2014 \u043f\u043e \u0432\u0435\u0440\u0442\u0438\u043a\u0430\u043b\u0438, \u0432\u043c\u0435\u0441\u0442\u0435 \u2014 \u043f\u043e \u043e\u0431\u0435\u0438\u043c \u043e\u0441\u044f\u043c. \u041f\u0440\u0438\u0432\u044f\u0437\u043a\u0430 \u043a \u0441\u0435\u0442\u043a\u0435 \u043b\u0438\u0431\u043e \u043a\u0440\u0430\u044f\u043c \u044d\u043a\u0440\u0430\u043d\u0430 \u0438 \u043f\u0430\u043d\u0435\u043b\u0435\u0439. \u0417\u0430\u043a\u0440\u0435\u043f\u043b\u0435\u043d\u0438\u0435 \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u0435\u0442 \u0438\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f; Esc \u0442\u043e\u0436\u0435 \u0437\u0430\u043a\u0440\u0435\u043f\u043b\u044f\u0435\u0442.");
    }
    ImGui::End();
    if(!available || ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {set_game_hud_editing(false);state.pending_save=true;}
}

void open_settings(DebugOverlay::Implementation& state, RendererConfig& config, bool first_run);

void draw_full_overlay(DebugOverlay& overlay, DebugOverlay::Implementation& state,
                       const RenderProfileSnapshot* profile,
                       const BridgeProfileSnapshot* bridge,
                       const RendererResourceSnapshot& resources,
                       const DebugResourceViewAvailability& debug_views,
                       RenderProfiler* profiler, const DebugShadowInfo& shadows) {
    ImGui::SetNextWindowSize(ImVec2(850.0f * state.ui_scale, 620.0f * state.ui_scale),
                             ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin(u8"\u0420\u0435\u043d\u0434\u0435\u0440 EID3D11", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled(state.developer
            ? u8"F10: \u0441\u043a\u0440\u044b\u0442\u044c / \u043c\u043e\u043d\u0438\u0442\u043e\u0440 / \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b   Shift+F10: \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0438\u0433\u0440\u043e\u043a\u0430   F11: \u0437\u0430\u0445\u0432\u0430\u0442 \u043a\u0430\u0434\u0440\u0430"
            : u8"Shift+F10: \u0441\u043a\u0440\u044b\u0442\u044c / \u043c\u043e\u043d\u0438\u0442\u043e\u0440 / \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u044b   F10: \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0438\u0433\u0440\u043e\u043a\u0430   F11: \u0437\u0430\u0445\u0432\u0430\u0442 \u043a\u0430\u0434\u0440\u0430");
        if (ImGui::BeginTabBar("RendererTabs")) {
            if (ImGui::BeginTabItem(u8"\u041f\u0440\u043e\u0438\u0437\u0432\u043e\u0434\u0438\u0442\u0435\u043b\u044c\u043d\u043e\u0441\u0442\u044c")) {
                draw_performance_tab(state, profile, bridge, resources);
                ImGui::Separator();
                if (ImGui::Button(u8"\u0417\u0430\u0445\u0432\u0430\u0442\u0438\u0442\u044c \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0438\u0439 \u043a\u0430\u0434\u0440 \u043f\u043e \u043f\u0440\u043e\u0445\u043e\u0434\u0430\u043c")) state.capture_requested = true;
                if (ImGui::Button(u8"\u0417\u0430\u0444\u0438\u043a\u0441\u0438\u0440\u043e\u0432\u0430\u0442\u044c / \u043e\u0441\u0432\u043e\u0431\u043e\u0434\u0438\u0442\u044c \u043a\u0430\u043c\u0435\u0440\u0443 \u043e\u0442\u0441\u0435\u0447\u0435\u043d\u0438\u044f (Shift+F11)")) state.cull_freeze_toggle = true;
                help_marker(u8"\u0414\u0438\u0430\u0433\u043d\u043e\u0441\u0442\u0438\u0447\u0435\u0441\u043a\u0438\u0439 \u0437\u0430\u0445\u0432\u0430\u0442 \u043c\u043e\u0436\u0435\u0442 \u0437\u0430\u0434\u0435\u0440\u0436\u0430\u0442\u044c \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0439 \u043a\u0430\u0434\u0440. \u041e\u0431\u044b\u0447\u043d\u044b\u0439 \u043c\u043e\u043d\u0438\u0442\u043e\u0440\u0438\u043d\u0433 \u043d\u0435 \u043f\u0438\u0448\u0435\u0442 \u043d\u0430 \u0434\u0438\u0441\u043a.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"\u0413\u0440\u0430\u0444\u0438\u043a\u0430")) {
                if (!state.settings_open && ImGui::Button(u8"\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044c\u0441\u043a\u0438\u0435 \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0433\u0440\u0430\u0444\u0438\u043a\u0438"))
                    open_settings(state, overlay.config(), false);
                draw_graphics_tab(overlay.config(), state.graphics_filter);
                ImGui::Separator();
                if (ImGui::Button(u8"\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c \u0438 \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c")) state.pending_save = true;
                ImGui::SameLine();
                ImGui::TextDisabled(u8"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u044e\u0442\u0441\u044f \u0432 \u0444\u0430\u0439\u043b\u0435 \u043a\u043e\u043d\u0444\u0438\u0433\u0443\u0440\u0430\u0446\u0438\u0438 \u0440\u0435\u043d\u0434\u0435\u0440\u0430");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(u8"\u0418\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441")) {
                draw_hud_layout_control(state,overlay.config());
                ImGui::SameLine();if(ImGui::Button(u8"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441"))reset_interface(overlay.config());
                draw_graphics_tab(overlay.config(),state.graphics_filter,false,true);
                if(ImGui::CollapsingHeader(u8"\u0417\u0434\u043e\u0440\u043e\u0432\u044c\u0435 \u0438 \u0431\u043e\u0435\u0432\u044b\u0435 \u044d\u0444\u0444\u0435\u043a\u0442\u044b"))state.combat_ui.draw_settings(overlay.config());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(renderer_ui_text_ru(kDiagnosticsGroup))) {
                draw_graphics_tab(overlay.config(), state.diagnostics_filter, true);
                ImGui::Separator();
                if (ImGui::Button(u8"\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u043c\u043e\u043b\u043d\u0438\u044e \u0432 \u0444\u043e\u043a\u0443\u0441\u0435 \u043a\u0430\u043c\u0435\u0440\u044b")) owned::request_debug_lightning();
                if (ImGui::Button(u8"\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c \u0438 \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c##diagnostics")) state.pending_save = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"\u0420\u0435\u0441\u0443\u0440\u0441\u044b")) {
                const DebugResourceView selected = overlay.config().debug_resource_view();
                if (ImGui::BeginCombo(u8"\u041f\u0440\u043e\u0441\u043c\u043e\u0442\u0440 \u0440\u0435\u0441\u0443\u0440\u0441\u0430",
                                      debug_resource_view_label(selected))) {
                    for (std::uint8_t raw = 0;
                         raw < static_cast<std::uint8_t>(DebugResourceView::count); ++raw) {
                        const auto candidate = static_cast<DebugResourceView>(raw);
                        const bool available = debug_views.available(candidate);
                        ImGui::BeginDisabled(!available);
                        if (ImGui::Selectable(debug_resource_view_label(candidate),
                                              candidate == selected) && available)
                            overlay.config().set_debug_resource_view(candidate);
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                if (!debug_views.available(selected))
                    ImGui::TextDisabled(u8"\u0412\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0439 \u0440\u0435\u0441\u0443\u0440\u0441 \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d \u0432 \u0442\u0435\u043a\u0443\u0449\u0435\u043c \u043a\u0430\u0434\u0440\u0435.");
                ImGui::TextWrapped(u8"\u041f\u043e\u043a\u0430\u0437\u044b\u0432\u0430\u044e\u0442\u0441\u044f \u0442\u043e\u043b\u044c\u043a\u043e \u0438\u043c\u0435\u044e\u0449\u0438\u0435\u0441\u044f \u0440\u0435\u0441\u0443\u0440\u0441\u044b \u0440\u0435\u043d\u0434\u0435\u0440\u0430. "
                                   u8"\u041e\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u044e\u0449\u0438\u0435 \u0431\u0443\u0444\u0435\u0440\u044b \u043e\u0442\u043c\u0435\u0447\u0430\u044e\u0442\u0441\u044f \u044f\u0432\u043d\u043e \u0438 \u043d\u0435 \u043f\u043e\u0434\u043c\u0435\u043d\u044f\u044e\u0442\u0441\u044f.");
                ImGui::TextDisabled(u8"\u041f\u0440\u043e\u0441\u043c\u043e\u0442\u0440 \u0434\u0435\u0439\u0441\u0442\u0432\u0443\u0435\u0442 \u0442\u043e\u043b\u044c\u043a\u043e \u043f\u043e\u043a\u0430 \u043e\u0442\u043a\u0440\u044b\u0442\u0430 \u0440\u0430\u0441\u0448\u0438\u0440\u0435\u043d\u043d\u0430\u044f \u043f\u0430\u043d\u0435\u043b\u044c F10.");
                if (selected == DebugResourceView::shadows ||
                    selected == DebugResourceView::shadow_cascade_coverage) {
                    ImGui::Separator();
                    ImGui::Text(u8"\u0422\u0435\u043d\u0438: %s", shadows.feature_enabled ? "ON" : "OFF");
                    ImGui::Text(u8"\u041c\u0430\u0441\u043a\u0438 \u0442\u0435\u043d\u0435\u0439 (\u0431\u0438\u0442\u044b C1..C4): \u0432\u043a\u043b\u044e\u0447\u0435\u043d\u044b 0x%X, \u0433\u043e\u0442\u043e\u0432\u044b 0x%X",
                                shadows.enabled_mask, shadows.valid_mask);
                    if (shadows.pass_in_progress)
                        ImGui::TextDisabled(u8"\u041f\u0440\u043e\u0445\u043e\u0434 \u0442\u0435\u043d\u0435\u0439 \u0432\u044b\u043f\u043e\u043b\u043d\u044f\u0435\u0442\u0441\u044f; \u043a\u0430\u0440\u0442\u044b \u043f\u043e\u043a\u0430 \u043d\u0435 \u043e\u0442\u043e\u0431\u0440\u0430\u0436\u0430\u044e\u0442\u0441\u044f.");
                    else
                        ImGui::Text(u8"\u041e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u044b \u0432 \u043f\u043e\u0441\u043b\u0435\u0434\u043d\u0435\u043c \u0437\u0430\u0432\u0435\u0440\u0448\u0451\u043d\u043d\u043e\u043c \u043f\u0440\u043e\u0445\u043e\u0434\u0435: 0x%X",
                                    shadows.rewritten_mask);
                    constexpr ImVec4 colors[] = {{1,.25f,.2f,1}, {.2f,1,.25f,1},
                                                 {.2f,.5f,1,1}, {1,.85f,.2f,1}, {.8f,.35f,1,1}};
                    ImGui::TextWrapped(u8"\u041a\u0430\u0440\u0442\u044b: C1\u2013C3 \u0432\u0432\u0435\u0440\u0445\u0443; C4 \u0441\u0440\u0435\u0434\u043d\u0435\u0439 \u0438 C5 \u0434\u0430\u043b\u044c\u043d\u0435\u0439 \u0434\u0438\u0441\u0442\u0430\u043d\u0446\u0438\u0438 \u0432\u043d\u0438\u0437\u0443. "
                        u8"\u0421\u0435\u0440\u044b\u0439 \u2014 \u043d\u043e\u0440\u043c\u0438\u0440\u043e\u0432\u0430\u043d\u043d\u0430\u044f \u0433\u043b\u0443\u0431\u0438\u043d\u0430 [0,1], \u0442\u0451\u043c\u043d\u043e-\u0441\u0438\u043d\u0438\u0439 \u2014 \u043f\u0443\u0441\u0442\u0430\u044f \u043e\u0431\u043b\u0430\u0441\u0442\u044c. "
                        u8"\u0426\u0432\u0435\u0442\u043d\u044b\u0435 \u043f\u0440\u044f\u043c\u043e\u0443\u0433\u043e\u043b\u044c\u043d\u0438\u043a\u0438 \u043f\u043e\u043a\u0430\u0437\u044b\u0432\u0430\u044e\u0442 \u0433\u0440\u0430\u043d\u0438\u0446\u044b \u043f\u0440\u0438\u0451\u043c\u043d\u0438\u043a\u043e\u0432 \u043f\u043e XY. \u0428\u0442\u0440\u0438\u0445\u043e\u0432\u043a\u0430 \u043e\u0431\u043e\u0437\u043d\u0430\u0447\u0430\u0435\u0442 \u043f\u0443\u0441\u0442\u044b\u0435 \u0438\u043b\u0438 \u043d\u0435\u0433\u043e\u0442\u043e\u0432\u044b\u0435 \u043a\u0430\u0440\u0442\u044b.");
                    ImGui::TextWrapped(u8"\u0426\u0432\u0435\u0442 \u043f\u043e\u043a\u0440\u044b\u0442\u0438\u044f: C1 \u043a\u0440\u0430\u0441\u043d\u044b\u0439, C2 \u0437\u0435\u043b\u0451\u043d\u044b\u0439, C3 \u0441\u0438\u043d\u0438\u0439, C4 \u0436\u0451\u043b\u0442\u044b\u0439, C5 \u0444\u0438\u043e\u043b\u0435\u0442\u043e\u0432\u044b\u0439. "
                        u8"\u0421\u0435\u0440\u044b\u0439 \u2014 \u043d\u0435\u0442 \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u043e\u0433\u043e \u043a\u0430\u0441\u043a\u0430\u0434\u0430, \u043f\u0443\u0440\u043f\u0443\u0440\u043d\u044b\u0439 \u2014 \u043f\u0435\u0440\u0432\u044b\u0439 \u043a\u0430\u0441\u043a\u0430\u0434 \u043d\u0435\u0433\u043e\u0442\u043e\u0432, \u0433\u043e\u043b\u0443\u0431\u043e\u0439 \u2014 \u0432\u043d\u0435 \u043e\u0431\u044a\u0451\u043c\u0430 \u0441\u0432\u0435\u0442\u0430. "
                        u8"\u041e\u0431\u044a\u0435\u043a\u0442\u044b \u0431\u0435\u0437 \u043e\u0441\u0432\u0435\u0449\u0435\u043d\u0438\u044f \u0438 \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441 \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u044e\u0442 \u0446\u0432\u0435\u0442\u0430. \u0418\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u0434\u0435\u0439\u0441\u0442\u0432\u0443\u044e\u0442 \u0441\u043e \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u0439 \u043e\u0442\u0440\u0438\u0441\u043e\u0432\u043a\u0438 \u043c\u0438\u0440\u0430.");
                    for (unsigned i = 0; i < 5; ++i) {
                        const auto& slice = shadows.slices[i];
                        const bool enabled = (shadows.enabled_mask & (1u << i)) != 0;
                        const bool valid = (shadows.valid_mask & (1u << i)) != 0;
                        ImGui::TextColored(colors[i], "C%u  %s", i + 1,
                                           !enabled ? u8"\u043f\u0443\u0441\u0442\u043e / \u043e\u0442\u043a\u043b\u044e\u0447\u0435\u043d\u043e" : (valid ? u8"\u0433\u043e\u0442\u043e\u0432\u043e" : u8"\u043d\u0435 \u0433\u043e\u0442\u043e\u0432\u043e"));
                        if (!enabled) continue;
                        ImGui::Text(u8"\u0420\u0430\u0434\u0438\u0443\u0441 %.4g  /  \u0435\u0434\u0438\u043d\u0438\u0446 \u043c\u0438\u0440\u0430 \u043d\u0430 \u0442\u0435\u043a\u0441\u0435\u043b\u044c %.6g  /  \u0432\u0435\u0440\u0441\u0438\u0439 \u043e\u0431\u044a\u0435\u043a\u0442\u043e\u0432 %llu",
                            slice.radius, slice.world_units_per_texel,
                            static_cast<unsigned long long>(slice.caster_revision_entries));
                        ImGui::Text(u8"\u0413\u0440\u0430\u043d\u0438\u0446\u044b \u043f\u0440\u0438\u0451\u043c\u043d\u0438\u043a\u043e\u0432 XY: [%.4g, %.4g] \u2014 [%.4g, %.4g]",
                            slice.receiver_minimum[0], slice.receiver_minimum[1],
                            slice.receiver_maximum[0], slice.receiver_maximum[1]);
                        ImGui::Text(u8"\u0413\u043b\u0443\u0431\u0438\u043d\u0430 \u043f\u0440\u0438\u0451\u043c\u043d\u0438\u043a\u043e\u0432: [%.4g, %.4g]  /  \u043a\u0430\u0440\u0442\u044b: [%.4g, %.4g]",
                            slice.receiver_minimum[2], slice.receiver_maximum[2],
                            slice.light_depth[0], slice.light_depth[1]);
                    }
                }
                if (ImGui::Button(u8"\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0439 \u043f\u0440\u043e\u0441\u043c\u043e\u0442\u0440")) state.pending_save = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"\u041c\u0430\u0440\u043a\u0435\u0440\u044b \u0437\u0430\u043c\u0435\u0440\u043e\u0432")) {
                constexpr const char* labels[] = {
                    "main_menu_high_fps_ui", "store_inventory", "city_heavy_forced_visibility",
                    "outdoor_daytime", "outdoor_nighttime", "cave_interior",
                    "local_light_effect_torch_spell_quest_emitter_mutation", "camera_pan_teleport",
                    "map_load_reload_transition_return_to_menu", "camera_motion_rotation_zoom",
                    "camera_turns", "terrain_foliage_water_vfx", "animated_figures_equipment",
                    "interaction_text_ui", "shadow_heavy_future" };
                const bool available = profiler && profiler->attribution_enabled();
                AttributionMarkerStatus status = profiler ? profiler->attribution_marker_status() :
                    AttributionMarkerStatus{};
                constexpr const char* states[] = {
                    u8"\u043d\u0435\u0430\u043a\u0442\u0438\u0432\u0435\u043d", u8"\u043e\u0436\u0438\u0434\u0430\u043d\u0438\u0435 \u043d\u0430\u0447\u0430\u043b\u0430", u8"\u0430\u043a\u0442\u0438\u0432\u0435\u043d", u8"\u043e\u0436\u0438\u0434\u0430\u043d\u0438\u0435 \u043a\u043e\u043d\u0446\u0430", u8"\u0437\u0430\u0432\u0435\u0440\u0448\u0451\u043d", u8"\u043f\u0435\u0440\u0435\u043f\u043e\u043b\u043d\u0435\u043d\u0438\u0435", u8"\u043d\u0435 \u0433\u043e\u0442\u043e\u0432\u043e" };
                const auto state_index = static_cast<std::size_t>(status.state);
                ImGui::Text(u8"\u0417\u0430\u043f\u0438\u0441\u044c: %s%s  \u0441\u0435\u0433\u043c\u0435\u043d\u0442\u043e\u0432: %u / %u",
                            state_index < std::size(states) ? states[state_index] : u8"\u043d\u0435 \u0433\u043e\u0442\u043e\u0432\u043e",
                            status.window_flush_pending ? u8" (\u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u0438\u0435 \u043e\u043a\u043d\u0430)" : "",
                            status.completed, status.capacity);
                ImGui::Text(u8"\u0422\u0435\u043a\u0443\u0449\u0435\u0435 \u043e\u043a\u043d\u043e \u0437\u0430\u043c\u0435\u0440\u0430: %u / %u \u043a\u0430\u0434\u0440\u043e\u0432",
                            status.window_frame_count, status.window_frame_capacity);
                ImGui::TextDisabled(u8"\u041f\u0440\u0438 \u0447\u0430\u0441\u0442\u043e\u0442\u0435 \u043e\u043a\u043e\u043b\u043e 600 FPS \u043e\u043a\u043d\u043e \u0438\u0437 8192 \u043a\u0430\u0434\u0440\u043e\u0432 \u0437\u0430\u043d\u0438\u043c\u0430\u0435\u0442 \u043f\u0440\u0438\u043c\u0435\u0440\u043d\u043e 13,65 \u0441.");
                if (!available)
                    ImGui::TextDisabled(u8"\u0414\u043b\u044f \u043c\u0430\u0440\u043a\u0435\u0440\u043e\u0432 \u043f\u0435\u0440\u0435\u0434 \u0437\u0430\u043f\u0443\u0441\u043a\u043e\u043c \u0437\u0430\u0434\u0430\u0439\u0442\u0435 EI_RENDERER_ATTRIBUTION=1.");
                ImGui::Combo(u8"\u041c\u0435\u0442\u043a\u0430 \u0441\u0446\u0435\u043d\u0430\u0440\u0438\u044f", &state.marker_label, labels,
                             static_cast<int>(std::size(labels)));
                ImGui::Checkbox(u8"\u041f\u0435\u0440\u0435\u0445\u043e\u0434 \u043e\u0442\u043a\u0440\u044b\u0442\u0438\u044f \u043c\u0435\u043d\u044e (\u0437\u0430\u0434\u0430\u0447\u0430 \u211613)",
                                &state.marker_issue13_transition);
                AttributionMarkerSelection selection{};
                selection.label = static_cast<AttributionMarkerLabel>(state.marker_label);
                selection.issue13_menu_transition = state.marker_issue13_transition;
                if (state.marker_issue13_transition) {
                    const auto menu_label_getter = [](void*, int index) -> const char* {
                        return attribution_issue13_menu_label_name(
                            static_cast<AttributionIssue13MenuLabel>(index));
                    };
                    ImGui::Combo(u8"\u041f\u043e\u0441\u0442\u043e\u044f\u043d\u043d\u0430\u044f \u043c\u0435\u0442\u043a\u0430 \u043c\u0435\u043d\u044e", &state.marker_menu_label,
                                 menu_label_getter, nullptr,
                                 static_cast<int>(AttributionIssue13MenuLabel::count));
                    ImGui::Text(u8"\u041d\u043e\u043c\u0435\u0440 \u043e\u0442\u043a\u0440\u044b\u0442\u0438\u044f: %d", state.marker_opening_ordinal);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(state.marker_opening_ordinal <= 1);
                    if (ImGui::Button("-##opening_ordinal")) --state.marker_opening_ordinal;
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(
                        state.marker_opening_ordinal == (std::numeric_limits<int>::max)());
                    if (ImGui::Button("+##opening_ordinal")) ++state.marker_opening_ordinal;
                    ImGui::EndDisabled();
                    ImGui::Combo(u8"\u041d\u0430\u0431\u043b\u044e\u0434\u0435\u043d\u0438\u0435 \u043e\u0442\u043a\u0440\u044b\u0442\u0438\u044f", &state.marker_repetition,
                                 "first_observed\0repeated_opening\0");
                    ImGui::TextDisabled(u8"\u041d\u043e\u043c\u0435\u0440 \u043e\u0442\u043a\u0440\u044b\u0442\u0438\u044f \u043d\u0435 \u043e\u043f\u0440\u0435\u0434\u0435\u043b\u044f\u0435\u0442, \u0431\u044b\u043b\u0438 \u043b\u0438 \u0440\u0435\u0441\u0443\u0440\u0441\u044b \u0443\u0436\u0435 \u0437\u0430\u0433\u0440\u0443\u0436\u0435\u043d\u044b.");
                    const char* menu_label = attribution_issue13_menu_label_name(
                        static_cast<AttributionIssue13MenuLabel>(state.marker_menu_label));
                    strncpy_s(selection.menu_label.data(), selection.menu_label.size(),
                              menu_label, _TRUNCATE);
                    selection.opening_ordinal = state.marker_opening_ordinal > 0 ?
                        static_cast<std::uint32_t>(state.marker_opening_ordinal) : 0;
                    selection.repetition = state.marker_repetition == 0 ?
                        AttributionMarkerRepetition::first_observed :
                        AttributionMarkerRepetition::repeated_opening;
                }
                const bool selection_ready = attribution_marker_selection_valid(selection);
                const bool can_begin = available &&
                    (status.state == AttributionMarkerState::inactive ||
                     status.state == AttributionMarkerState::complete) &&
                    !status.window_flush_pending && selection_ready;
                if (available && !can_begin) {
                    if (!selection_ready)
                        ImGui::TextDisabled(u8"\u041d\u0430\u0447\u0430\u043b\u043e \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e: \u0437\u0430\u043f\u043e\u043b\u043d\u0438\u0442\u0435 \u0434\u0430\u043d\u043d\u044b\u0435 \u043c\u0430\u0440\u043a\u0435\u0440\u0430.");
                    else if (status.window_flush_pending)
                        ImGui::TextDisabled(u8"\u041d\u0430\u0447\u0430\u043b\u043e \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e: \u0434\u043e\u0436\u0434\u0438\u0442\u0435\u0441\u044c \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u044f \u0437\u0430\u0432\u0435\u0440\u0448\u0451\u043d\u043d\u043e\u0433\u043e \u043e\u043a\u043d\u0430.");
                    else
                        ImGui::TextDisabled(u8"\u041d\u0430\u0447\u0430\u043b\u043e \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e: \u0437\u0430\u0432\u0435\u0440\u0448\u0438\u0442\u0435 \u0438\u043b\u0438 \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u0435 \u0442\u0435\u043a\u0443\u0449\u0443\u044e \u0437\u0430\u043f\u0438\u0441\u044c.");
                }
                ImGui::BeginDisabled(!can_begin);
                if (ImGui::Button(u8"\u041d\u0430\u0447\u0430\u0442\u044c \u043f\u0440\u0438 \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u043c \u0443\u0441\u043f\u0435\u0448\u043d\u043e\u043c \u043f\u043e\u043a\u0430\u0437\u0435 \u043a\u0430\u0434\u0440\u0430")) {
                    profiler->arm_attribution_marker_begin(selection);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                const bool can_end = available && status.state == AttributionMarkerState::active;
                ImGui::BeginDisabled(!can_end);
                if (ImGui::Button(u8"\u0417\u0430\u0432\u0435\u0440\u0448\u0438\u0442\u044c \u043f\u0440\u0438 \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u043c \u0443\u0441\u043f\u0435\u0448\u043d\u043e\u043c \u043f\u043e\u043a\u0430\u0437\u0435 \u043a\u0430\u0434\u0440\u0430"))
                    profiler->arm_attribution_marker_end();
                ImGui::EndDisabled();
                ImGui::TextWrapped(u8"\u041c\u0430\u0440\u043a\u0435\u0440\u044b \u0437\u0430\u0434\u0430\u044e\u0442 \u0438\u043d\u0442\u0435\u0440\u0432\u0430\u043b\u044b \u043f\u043e\u043a\u0430\u0437\u0430\u043d\u043d\u044b\u0445 \u043a\u0430\u0434\u0440\u043e\u0432 \u0441 \u0438\u0441\u043a\u043b\u044e\u0447\u0451\u043d\u043d\u043e\u0439 \u043f\u0440\u0430\u0432\u043e\u0439 \u0433\u0440\u0430\u043d\u0438\u0446\u0435\u0439. \u041e\u043a\u043d\u0430 \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u044e\u0442\u0441\u044f \u043f\u043e\u0441\u043b\u0435 \u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u0438\u044f \u043e\u0442\u043b\u043e\u0436\u0435\u043d\u043d\u044b\u0445 \u0437\u0430\u043f\u0440\u043e\u0441\u043e\u0432 GPU \u0432 \u043f\u0440\u043e\u043c\u0435\u0436\u0443\u0442\u043a\u0435 \u043c\u0435\u0436\u0434\u0443 \u0437\u0430\u043c\u0435\u0440\u0430\u043c\u0438. \u0410\u0432\u0442\u043e\u043e\u043f\u0440\u0435\u0434\u0435\u043b\u0435\u043d\u0438\u0435 \u0441\u0446\u0435\u043d, \u0438\u0433\u0440\u043e\u0432\u044b\u0435 \u043f\u0435\u0440\u0435\u0445\u0432\u0430\u0442\u044b \u0438 \u0433\u043b\u043e\u0431\u0430\u043b\u044c\u043d\u044b\u0435 \u0433\u043e\u0440\u044f\u0447\u0438\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0438 \u043c\u0430\u0440\u043a\u0435\u0440\u043e\u0432 \u043d\u0435 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u044e\u0442\u0441\u044f.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!open) overlay.set_mode(DebugOverlayMode::hidden);
}

void restore_options(RendererConfig& config,
                     const std::vector<DebugOverlay::Implementation::SavedValue>& saved) {
    const auto& options = config.options();
    for (std::size_t index = 0; index < saved.size() && index < options.size(); ++index) {
        const char* key = options[index].key.c_str();
        switch (options[index].type) {
        case RendererOptionType::boolean: config.set_boolean(key, saved[index].boolean); break;
        case RendererOptionType::integer: config.set_integer(key, saved[index].integer); break;
        case RendererOptionType::floating_point: config.set_floating_point(key, saved[index].floating); break;
        case RendererOptionType::text: config.set_text(key,saved[index].text.c_str()); break;
        }
    }
}

void open_settings(DebugOverlay::Implementation& state, RendererConfig& config, bool first_run) {
    try {
        state.settings_snapshot.clear();
        for (const auto& option : config.options())
            state.settings_snapshot.push_back({option.boolean_value, option.integer_value, option.floating_value,option.text_value});
    } catch (const std::bad_alloc&) {
        return;  // No snapshot, no window; F10 can try again.
    }
    state.settings_open = true;
    state.settings_first_run = first_run;
    state.settings_first_frame = true;
    // First run previews the suggestion; OK keeps it.
    if (first_run && state.suggested_preset != GraphicsPreset::custom)
        apply_graphics_preset(config, state.suggested_preset);
    const GraphicsPreset detected = detect_graphics_preset(config);
    if (detected != GraphicsPreset::custom) state.preset_slider = static_cast<int>(detected);
}

// Options apply live while the window is open; keep saves them, otherwise
// the values from when the window opened are restored.
void close_settings(DebugOverlay::Implementation& state, RendererConfig& config, bool keep) {
    if (keep) {
        if (state.settings_first_run) config.set_first_run_done(true);
        state.pending_save = true;
    } else {
        restore_options(config, state.settings_snapshot);
    }
    state.settings_open = false;
    state.settings_first_run = false;
    state.settings_snapshot.clear();
}

void draw_player_setting(RendererConfig& config, const PlayerGraphicsSetting& setting, bool developer,
                         DebugOverlay::Implementation& state) {
    const RendererOption* option = config.find(setting.key);
    if (!option) {
        // Visible to developers only: a table key the renderer never registered.
        if (developer) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(1.f, .35f, .2f, 1.f), u8"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0430 \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d\u0430: %s", setting.key);
        }
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", setting.label);
    ImGui::SetItemTooltip("%s", setting.tooltip);
    ImGui::TableNextColumn();
    ImGui::PushID(setting.key);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const char* note = blocked_option_note(config, setting.key);
    ImGui::BeginDisabled(note != nullptr);
    switch (option->type) {
    case RendererOptionType::boolean: {
        bool value = option->boolean_value;
        if (ImGui::Checkbox("##value", &value)) config.set_boolean(setting.key, value);
        break;
    }
    case RendererOptionType::integer: {
        if (std::strcmp(setting.key, "render_scale") == 0) {
            char preview[16]{};
            std::snprintf(preview, sizeof(preview), "%d%%", option->integer_value);
            if (ImGui::BeginCombo("##value", preview)) {
                for (const int percent : kRenderScaleChoices) {
                    char label[16]{};
                    std::snprintf(label, sizeof(label), "%d%%", percent);
                    if (ImGui::Selectable(label, percent == option->integer_value))
                        config.set_integer(setting.key, percent);
                }
                ImGui::EndCombo();
            }
        } else if (std::strcmp(setting.key, "window_width") == 0) {
            // One row for both client dimensions; 0 x 0 is the monitor's size.
            int width = option->integer_value, height = configured_value(config, "window_height");
            char preview[64] = u8"\u0420\u0430\u0437\u043c\u0435\u0440 \u043c\u043e\u043d\u0438\u0442\u043e\u0440\u0430";
            if (width > 0 && height > 0) format_window_size(preview, sizeof(preview), width, height);
            if (ImGui::BeginCombo("##value", preview)) {
                if (ImGui::Selectable(u8"\u0420\u0430\u0437\u043c\u0435\u0440 \u043c\u043e\u043d\u0438\u0442\u043e\u0440\u0430", width <= 0 || height <= 0)) {
                    config.set_integer("window_width", 0);
                    config.set_integer("window_height", 0);
                }
                for (const auto& size : state.window_sizes) {
                    char label[64]{};
                    format_window_size(label, sizeof(label), size.first, size.second);
                    if (ImGui::Selectable(label, size.first == width && size.second == height)) {
                        config.set_integer("window_width", size.first);
                        config.set_integer("window_height", size.second);
                    }
                }
                ImGui::EndCombo();
            }
            width = configured_value(config, "window_width");
            height = configured_value(config, "window_height");
            const float field_width = (std::max)(1.f, (ImGui::GetContentRegionAvail().x -
                ImGui::CalcTextSize("x").x - 2.f * ImGui::GetStyle().ItemSpacing.x) * .5f);
            ImGui::SetNextItemWidth(field_width);
            if (ImGui::InputInt("##width", &width, 0, 0)) config.set_integer("window_width", width);
            ImGui::SetItemTooltip(u8"\u0428\u0438\u0440\u0438\u043d\u0430 \u043e\u0431\u043b\u0430\u0441\u0442\u0438 \u0438\u0433\u0440\u044b \u0432 \u043f\u0438\u043a\u0441\u0435\u043b\u044f\u0445. \u0423\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u0435 \u043e\u0431\u0430 \u0440\u0430\u0437\u043c\u0435\u0440\u0430 \u0432 0 \u0434\u043b\u044f \u0440\u0430\u0437\u043c\u0435\u0440\u0430 \u043c\u043e\u043d\u0438\u0442\u043e\u0440\u0430.");
            ImGui::SameLine();
            ImGui::TextUnformatted("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(field_width);
            if (ImGui::InputInt("##height", &height, 0, 0)) config.set_integer("window_height", height);
            ImGui::SetItemTooltip(u8"\u0412\u044b\u0441\u043e\u0442\u0430 \u043e\u0431\u043b\u0430\u0441\u0442\u0438 \u0438\u0433\u0440\u044b \u0432 \u043f\u0438\u043a\u0441\u0435\u043b\u044f\u0445. \u0421\u043b\u0438\u0448\u043a\u043e\u043c \u0431\u043e\u043b\u044c\u0448\u043e\u0435 \u043e\u043a\u043d\u043e \u0443\u043c\u0435\u043d\u044c\u0448\u0430\u0435\u0442\u0441\u044f \u0434\u043e \u0440\u0430\u0431\u043e\u0447\u0435\u0439 \u043e\u0431\u043b\u0430\u0441\u0442\u0438 \u044d\u043a\u0440\u0430\u043d\u0430.");
            ImGui::BeginDisabled(state.display_apply_pending);
            if (ImGui::Button(u8"\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c \u044d\u043a\u0440\u0430\u043d")) {
                state.display_apply = {configured_value(config, "display_mode") == 1 ? DisplayMode::windowed : DisplayMode::borderless,
                    configured_value(config, "window_width"), configured_value(config, "window_height")};
                state.display_apply_pending = true;
                state.display_apply_result[0] = 0;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(u8"\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c \u0431\u0435\u0437 \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u043a\u0430. \u0411\u0435\u0437\u0440\u0430\u043c\u043e\u0447\u043d\u044b\u0439 \u0440\u0435\u0436\u0438\u043c \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0435\u0442 \u0440\u0430\u0437\u043c\u0435\u0440 \u043c\u043e\u043d\u0438\u0442\u043e\u0440\u0430, \u043e\u043a\u043e\u043d\u043d\u044b\u0439 \u043e\u0433\u0440\u0430\u043d\u0438\u0447\u0438\u0432\u0430\u0435\u0442\u0441\u044f \u0440\u0430\u0431\u043e\u0447\u0435\u0439 \u043e\u0431\u043b\u0430\u0441\u0442\u044c\u044e \u044d\u043a\u0440\u0430\u043d\u0430.");
            if (state.display_apply_result[0]) ImGui::TextWrapped("%s", state.display_apply_result);
        } else if (std::strcmp(setting.key, "frame_limit") == 0) {
            // Off / fixed rates / Custom; Custom shows a rate slider below.
            const char* labels = u8"\u0412\u044b\u043a\u043b.\0" "30\0" "60\0" "90\0" "120\0" "144\0" "165\0" "240\0" u8"\u0421\u0432\u043e\u0451\0";
            int index = frame_limit_choice(option->integer_value);
            if (ImGui::Combo("##value", &index, labels)) {
                // Custom keeps an existing custom rate, else starts at 75.
                const bool already_custom = frame_limit_choice(option->integer_value) == kFrameLimitChoiceCount;
                const int custom = already_custom ? option->integer_value : 75;
                config.set_integer(setting.key, index < kFrameLimitChoiceCount ? kFrameLimitChoices[index] : custom);
            }
            if (frame_limit_choice(option->integer_value) == kFrameLimitChoiceCount) {
                int fps = option->integer_value;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderInt("##custom", &fps, kFrameLimitCustomMinimum, option->integer_maximum, "%d FPS"))
                    config.set_integer(setting.key, fps);
            }
        } else if (setting.choices) {
            int index = option->integer_value - option->integer_minimum;
            if (ImGui::Combo("##value", &index, setting.choices))
                config.set_integer(setting.key, option->integer_minimum + index);
        } else {
            int value = option->integer_value;
            const bool fade_transparency=std::strstr(setting.key,"_fade_transparency")!=nullptr;
            const char* format=(std::strcmp(setting.key,"hud_fade_duration")==0 || std::strcmp(setting.key,"hud_fade_in_duration")==0 || std::strcmp(setting.key,"hud_fade_delay")==0)?u8"%d \u043c\u0441":
                fade_transparency?(value==0?u8"%d%% (\u0432\u0438\u0434\u043d\u0430)":value==100?u8"%d%% (\u0441\u043a\u0440\u044b\u0442\u0430)":"%d%%"):"%d";
            if (ImGui::SliderInt("##value", &value, option->integer_minimum, option->integer_maximum,format)) {
                const int step = std::max(1, option->integer_step);
                value = option->integer_minimum + (value - option->integer_minimum + step / 2) / step * step;
                config.set_integer(setting.key, value);
            }
        }
        break;
    }
    case RendererOptionType::floating_point: {
        float value = option->floating_value;
        if (ImGui::SliderFloat("##value", &value, option->floating_minimum, option->floating_maximum, "%.2f"))
            config.set_floating_point(setting.key, value);
        break;
    }
    }
    ImGui::EndDisabled();
    if (note) ImGui::TextDisabled("%s", note);
    ImGui::SetItemTooltip("%s", setting.tooltip);
    ImGui::PopID();
}

void draw_settings_window(DebugOverlay::Implementation& state, RendererConfig& config) {
    const float scale = state.ui_scale;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f), ImGuiCond_Appearing,
                            ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(860.f * scale, io.DisplaySize.x * .95f),
                                    std::min(660.f * scale, io.DisplaySize.y * .9f)), ImGuiCond_Appearing);
    bool keep_open = true;
    bool accept = false, cancel = false;
    if (ImGui::Begin(u8"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438", state.settings_first_run ? nullptr : &keep_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        if (state.settings_first_frame) {
            ImGui::SetWindowFocus();
            state.settings_first_frame = false;
        }
        if (state.settings_first_run) {
            ImGui::TextWrapped(u8"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043a\u0430\u0447\u0435\u0441\u0442\u0432\u043e \u0433\u0440\u0430\u0444\u0438\u043a\u0438 \u0434\u043b\u044f \u044d\u0442\u043e\u0433\u043e \u043a\u043e\u043c\u043f\u044c\u044e\u0442\u0435\u0440\u0430. "
                               u8"\u0415\u0433\u043e \u043c\u043e\u0436\u043d\u043e \u0438\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u0432 \u043b\u044e\u0431\u043e\u0435 \u0432\u0440\u0435\u043c\u044f \u043a\u043b\u0430\u0432\u0438\u0448\u0435\u0439 F10.");
            if (state.suggested_preset != GraphicsPreset::custom)
                ImGui::Text(u8"\u0420\u0435\u043a\u043e\u043c\u0435\u043d\u0434\u0443\u0435\u0442\u0441\u044f \u0434\u043b\u044f %s: %s", state.adapter_name[0] ? state.adapter_name : u8"\u044d\u0442\u043e\u0439 \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u044b",
                            graphics_preset_name(state.suggested_preset));
        }

        if (!state.large_address_aware) {
            ImGui::TextColored(ImVec4(1.f, .8f, .3f, 1.f), u8"\u0420\u0435\u0436\u0438\u043c 4 \u0413\u0411 \u043f\u0430\u043c\u044f\u0442\u0438 \u043e\u0442\u043a\u043b\u044e\u0447\u0451\u043d");
            ImGui::SetItemTooltip(u8"\u0418\u0433\u0440\u0430 \u043c\u043e\u0436\u0435\u0442 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u044c \u0442\u043e\u043b\u044c\u043a\u043e 2 \u0413\u0411 \u043f\u0430\u043c\u044f\u0442\u0438. \u041d\u0430 \u0431\u043e\u043b\u044c\u0448\u0438\u0445 \u043a\u0430\u0440\u0442\u0430\u0445 \u0441 \u0432\u044b\u0441\u043e\u043a\u0438\u043c \u043a\u0430\u0447\u0435\u0441\u0442\u0432\u043e\u043c \u043f\u0430\u043c\u044f\u0442\u0438 \u043c\u043e\u0436\u0435\u0442 \u043d\u0435 \u0445\u0432\u0430\u0442\u0438\u0442\u044c.");
            if (state.large_address_result) {
                ImGui::TextWrapped("%s", state.large_address_result);
            } else if (ImGui::Button(u8"\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c 4 \u0413\u0411 (\u043d\u0443\u0436\u0435\u043d \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u043a)")) {
                state.large_address_result = enable_large_address_mode();
            }
        }
        if (state.developer)
            ImGui::TextDisabled(u8"\u0420\u0435\u0436\u0438\u043c \u0440\u0430\u0437\u0440\u0430\u0431\u043e\u0442\u0447\u0438\u043a\u0430: \u0432\u0435\u0440\u0442\u0438\u043a\u0430\u043b\u044c\u043d\u0430\u044f \u0441\u0438\u043d\u0445\u0440\u043e\u043d\u0438\u0437\u0430\u0446\u0438\u044f \u0438 \u043e\u0433\u0440\u0430\u043d\u0438\u0447\u0435\u043d\u0438\u0435 FPS \u043f\u0440\u0438\u043d\u0443\u0434\u0438\u0442\u0435\u043b\u044c\u043d\u043e \u043e\u0442\u043a\u043b\u044e\u0447\u0435\u043d\u044b.");
        const GraphicsPreset detected = detect_graphics_preset(config);
        if (detected != GraphicsPreset::custom) state.preset_slider = static_cast<int>(detected);
        ImGui::SeparatorText(u8"\u041a\u0430\u0447\u0435\u0441\u0442\u0432\u043e \u0433\u0440\u0430\u0444\u0438\u043a\u0438");
        ImGui::SetNextItemWidth(-FLT_MIN);
        int slider = state.preset_slider;
        if (ImGui::SliderInt("##preset", &slider, 0, kGraphicsPresetCount - 1, graphics_preset_name(detected),
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            state.preset_slider = slider;
            apply_graphics_preset(config, static_cast<GraphicsPreset>(slider));
        }
        ImGui::SetItemTooltip(u8"\u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043d\u0438\u0437\u043a\u043e\u0435, \u0441\u0440\u0435\u0434\u043d\u0435\u0435, \u0432\u044b\u0441\u043e\u043a\u043e\u0435 \u0438\u043b\u0438 \u0443\u043b\u044c\u0442\u0440\u0430\u043a\u0430\u0447\u0435\u0441\u0442\u0432\u043e. \u0418\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u0435 \u043e\u0442\u0434\u0435\u043b\u044c\u043d\u044b\u0445 \u043f\u0430\u0440\u0430\u043c\u0435\u0442\u0440\u043e\u0432 \u043f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0430\u0435\u0442 \u043f\u0440\u043e\u0444\u0438\u043b\u044c \u043d\u0430 \u00ab\u0421\u0432\u043e\u0451\u00bb.");
        // Stop labels under the slider.
        const float width = ImGui::GetContentRegionAvail().x;
        const float left = ImGui::GetCursorPosX();
        for (int index = 0; index < kGraphicsPresetCount; ++index) {
            const char* name = graphics_preset_name(static_cast<GraphicsPreset>(index));
            const float text = ImGui::CalcTextSize(name).x;
            const float at = left + (width - text) * static_cast<float>(index) / (kGraphicsPresetCount - 1);
            if (index) ImGui::SameLine();
            ImGui::SetCursorPosX(at);
            ImGui::TextDisabled("%s", name);
        }
        ImGui::TextWrapped("%s: %s", graphics_preset_name(detected), graphics_preset_summary(detected));
        if (const auto* strength = config.find("depth_of_field_strength"))
            if (configured_value(config, "depth_of_field") > 0 && strength->floating_value <= 0.f)
                ImGui::TextWrapped(u8"\u0420\u0430\u0437\u043c\u044b\u0442\u0438\u0435 \u0444\u043e\u043d\u0430 \u043d\u0435 \u0434\u0435\u0439\u0441\u0442\u0432\u0443\u0435\u0442 \u043f\u0440\u0438 \u043d\u0443\u043b\u0435\u0432\u043e\u0439 \u0441\u0438\u043b\u0435. \u0423\u0432\u0435\u043b\u0438\u0447\u044c\u0442\u0435 \u0441\u0438\u043b\u0443 \u0440\u0430\u0437\u043c\u044b\u0442\u0438\u044f. \u041f\u0440\u0438 \u0432\u0438\u0434\u0435 \u0441\u0432\u0435\u0440\u0445\u0443 \u0438\u0437\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u0435 \u043e\u0441\u0442\u0430\u0451\u0442\u0441\u044f \u0447\u0451\u0442\u043a\u0438\u043c.");

        if (configured_value(config, "display_mode") != state.running_display[0] ||
            configured_value(config, "window_width") != state.running_display[1] ||
            configured_value(config, "window_height") != state.running_display[2])
            ImGui::TextDisabled(u8"\u0420\u0435\u0436\u0438\u043c \u044d\u043a\u0440\u0430\u043d\u0430 \u0435\u0449\u0451 \u043d\u0435 \u0438\u0437\u043c\u0435\u043d\u0451\u043d. \u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u00ab\u041f\u0440\u0438\u043c\u0435\u043d\u0438\u0442\u044c \u044d\u043a\u0440\u0430\u043d\u00bb \u0440\u044f\u0434\u043e\u043c \u0441 \u0440\u0430\u0437\u043c\u0435\u0440\u043e\u043c \u043e\u043a\u043d\u0430.");
        state.settings_filter.Draw(u8"\u041f\u043e\u0438\u0441\u043a \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a",-1.f);
        const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeightWithSpacing()*2.f;
        if(ImGui::BeginTabBar("option_tabs")) {
          for(unsigned tab=0;tab<2;++tab) {
            const bool ui_tab=tab==1;
            if(!ImGui::BeginTabItem(ui_tab?u8"\u0418\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441":u8"\u0413\u0440\u0430\u0444\u0438\u043a\u0430"))continue;
            if(ui_tab) {
                draw_hud_layout_control(state,config);ImGui::SameLine();
                if(ImGui::Button(u8"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441"))reset_interface(config);
            }
            if(ImGui::BeginChild(ui_tab?"interface_options":"graphics_options",ImVec2(0,-footer),ImGuiChildFlags_Borders)) {
            std::size_t category_count = 0, setting_count = 0;
            const char* const* categories = player_graphics_categories(category_count);
            const PlayerGraphicsSetting* settings = player_graphics_settings(setting_count);
            auto matches = [&](const PlayerGraphicsSetting& setting) {
                const std::string text = std::string(setting.label) + " " + setting.key + " " +
                                         setting.category + " " + setting.tooltip;
                return state.settings_filter.PassFilter(text.c_str());
            };
            bool any_match = false;
            for (std::size_t category = 0; category < category_count; ++category) {
                if((std::strcmp(categories[category],u8"\u0418\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441")==0)!=ui_tab)continue;
                bool category_match = false;
                for (std::size_t index = 0; index < setting_count; ++index)
                    if (std::strcmp(settings[index].category, categories[category]) == 0 && matches(settings[index]))
                        category_match = true;
                if (!category_match) continue;
                any_match = true;
                if(ui_tab || state.settings_filter.IsActive())ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                if (!ImGui::CollapsingHeader(categories[category])) continue;
                if (ImGui::BeginTable(categories[category], 2,
                                      ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
                    for (std::size_t index = 0; index < setting_count; ++index)
                        if (std::strcmp(settings[index].category, categories[category]) == 0 && matches(settings[index]))
                            draw_player_setting(config, settings[index], state.developer, state);
                    ImGui::EndTable();
                }
            }
            if(ui_tab && ImGui::CollapsingHeader(u8"\u0417\u0434\u043e\u0440\u043e\u0432\u044c\u0435 \u0438 \u0431\u043e\u0435\u0432\u044b\u0435 \u044d\u0444\u0444\u0435\u043a\u0442\u044b"))state.combat_ui.draw_settings(config);
            if (!any_match) ImGui::TextDisabled(u8"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d\u044b.");
            }
            ImGui::EndChild();ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }

        if (ImGui::Button(u8"\u041e\u041a", ImVec2(110.f * scale, 0.f))) accept = true;
        if (!state.settings_first_run) {
            ImGui::SameLine();
            if (ImGui::Button(u8"\u041e\u0442\u043c\u0435\u043d\u0430", ImVec2(110.f * scale, 0.f))) cancel = true;
        }
        ImGui::TextWrapped(state.settings_first_run ? u8"\u041e\u041a \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u0435\u0442 \u0432\u044b\u0431\u043e\u0440."
                                                     : u8"\u0418\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u0432\u0438\u0434\u043d\u044b \u0441\u0440\u0430\u0437\u0443. \u041e\u041a \u2014 \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c, \u041e\u0442\u043c\u0435\u043d\u0430 \u0438\u043b\u0438 Esc \u2014 \u0432\u0435\u0440\u043d\u0443\u0442\u044c.");
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            (state.settings_first_run ? accept : cancel) = true;
    }
    ImGui::End();
    if (!keep_open) cancel = true;
    if (accept) close_settings(state, config, true);
    else if (cancel) close_settings(state, config, false);
}

// First-run hint: UMA adapters expose no non-local segment; discrete cards
// are graded by their local budget (DXGI_ADAPTER_DESC sizes are 32-bit here).
void suggest_for_adapter(DebugOverlay::Implementation& state, ID3D11Device* device) {
    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device)))) return;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgi_device->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC desc{};
        std::uint64_t memory = 0;
        bool integrated = false;
        if (SUCCEEDED(adapter->GetDesc(&desc))) {
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, state.adapter_name,
                                static_cast<int>(sizeof(state.adapter_name)), nullptr, nullptr);
            state.adapter_name[sizeof(state.adapter_name) - 1] = '\0';
            memory = desc.DedicatedVideoMemory;
            integrated = desc.DedicatedVideoMemory < 512ull * 1024ull * 1024ull;
        }
        IDXGIAdapter3* adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3)))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, non_local{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
                SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local))) {
                memory = local.Budget;
                integrated = non_local.Budget == 0;
            }
            adapter3->Release();
        }
        state.suggested_preset = suggest_graphics_preset(integrated, memory);
        adapter->Release();
    }
    dxgi_device->Release();
}

} // namespace

DebugOverlay::DebugOverlay() = default;

bool renderer_controls_capture_input(HWND window) noexcept {
    DWORD_PTR reference = 0;
    if (!GetWindowSubclass(window, overlay_subclass_proc, kOverlaySubclassId, &reference)) return false;
    const auto* overlay = reinterpret_cast<const DebugOverlay*>(reference);
    return overlay && overlay->initialized() && overlay->captures_input();
}

DebugOverlay::~DebugOverlay() {
    shutdown();
}

HRESULT DebugOverlay::initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context,
                                 const wchar_t* config_path) {
    shutdown();
    if (!window || !device || !context) return E_INVALIDARG;
    auto state = std::make_unique<Implementation>();
    state->window = window;
    state->device = device;
    state->context = context;
    state->config_path = config_path && *config_path ? config_path : renderer_config_path();
    config_.load(state->config_path.c_str());
    // The proxy sized the window from these values before the renderer existed.
    state->running_display = {configured_value(config_, "display_mode"), configured_value(config_, "window_width"),
                              configured_value(config_, "window_height")};
    state->window_sizes = monitor_window_sizes(window);
    state->developer = renderer_developer_mode();
    state->large_address_aware = running_image_large_address_aware();
    remove_previous_patched_image();
    // Players start without developer windows unless their ini chose one.
    if (!state->developer && !config_.overlay_mode_loaded()) config_.set_overlay_mode(DebugOverlayMode::hidden);
    if (first_run_settings_due(config_.first_run_done(), state->developer))
        suggest_for_adapter(*state, device);
    state->f10_was_down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    state->f11_was_down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    state->ui_scale = overlay_scale_for_window(window);
    for (std::size_t index = 0; index < kMouseVirtualKeys.size(); ++index)
        state->mouse_was_down[index] =
            (GetAsyncKeyState(kMouseVirtualKeys[index]) & 0x8000) != 0;

    IMGUI_CHECKVERSION();
    state->imgui = ImGui::CreateContext();
    if (!state->imgui) return E_OUTOFMEMORY;
    ImGui::SetCurrentContext(state->imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The built-in Latin-only font cannot display localized settings.
    ImFont* settings_font = nullptr;
    wchar_t windows_directory[MAX_PATH]{};
    const UINT windows_length = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    if (windows_length > 0 && windows_length < MAX_PATH) {
        for (const wchar_t* filename : {L"\\Fonts\\segoeui.ttf", L"\\Fonts\\tahoma.ttf"}) {
            const std::wstring path = std::wstring(windows_directory) + filename;
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) continue;
            const DWORD size = GetFileSize(file, nullptr);
            void* bytes = size > 0 && size < 16u * 1024u * 1024u ? ImGui::MemAlloc(size) : nullptr;
            DWORD read = 0;
            const bool complete = bytes && ReadFile(file, bytes, size, &read, nullptr) && read == size;
            CloseHandle(file);
            if (!complete) { if (bytes) ImGui::MemFree(bytes); continue; }
            settings_font = io.Fonts->AddFontFromMemoryTTF(bytes, static_cast<int>(size), 16.f,
                                                         nullptr, io.Fonts->GetGlyphRangesCyrillic());
            if (settings_font) break;
        }
    }
    if (settings_font) io.FontDefault = settings_font;
    else io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = state->ui_scale;
    style.ScaleAllSizes(state->ui_scale);
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;

    if (!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(device, context)) {
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(state->imgui);
        return E_FAIL;
    }
    implementation_ = std::move(state);
    implementation_->combat_ui.initialize(device, context);
    if (!SetWindowSubclass(window, overlay_subclass_proc, kOverlaySubclassId,
                           reinterpret_cast<DWORD_PTR>(this))) {
        shutdown();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    bridge_set_camp_input_overlay(this);
    return S_OK;
}

bool DebugOverlay::process_camp_input(UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if(!initialized())return false;
    const HWND window=implementation_->window;
    const bool controls=captures_input();
    if(camp_inventory_input(window,message,wparam,game_native_mouse_message(message,lparam,true),controls))return true;
    if(!controls&&message==WM_MOUSEWHEEL){
        POINT point{LONG(static_cast<short>(LOWORD(lparam))),LONG(static_cast<short>(HIWORD(lparam)))};
        return ScreenToClient(window,&point)&&camp_inventory_wheel(game_output_to_native(window,point),static_cast<short>(HIWORD(wparam)),LOWORD(wparam));
    }
    return false;
}

void DebugOverlay::shutdown() noexcept {
    set_game_hud_editing(false);
    bridge_set_camp_input_overlay(nullptr);
    camp_inventory_cancel_drag();
    if (!implementation_) return;
    if (implementation_->imgui) ImGui::SetCurrentContext(implementation_->imgui);
    implementation_->combat_ui.shutdown();
    release_cursor_confinement(implementation_->cursor_confinement);
    if (GetCapture() == implementation_->window) ReleaseCapture();
    if (implementation_->window)
        RemoveWindowSubclass(implementation_->window, overlay_subclass_proc, kOverlaySubclassId);
    if (implementation_->imgui) {
        ImGui::SetCurrentContext(implementation_->imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(implementation_->imgui);
    }
    implementation_.reset();
}

bool DebugOverlay::process_combat_input(UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if (message==WM_MOUSEWHEEL && captures_input()) return false;
    return implementation_ && implementation_->combat_ui.process_input(message,wparam,lparam);
}

HRESULT DebugOverlay::render(const RenderProfileSnapshot* profile,
                             const BridgeProfileSnapshot* bridge,
                             const RendererResourceSnapshot& resources,
                             const DebugResourceViewAvailability& debug_views,
                             RenderProfiler* profiler, const DebugShadowInfo& shadows) {
    if (!implementation_ || !implementation_->imgui) return E_UNEXPECTED;
    ImGui::SetCurrentContext(implementation_->imgui);
    maintain();
    const bool capture_notice = implementation_->capture_warmup || implementation_->capture_requested ||
        GetTickCount64() < implementation_->capture_notice_until;
    const bool combat_visible = implementation_->combat_ui.prepare(config_);
    if (!combat_visible && !visible() && resources.critters.labels.empty() && !notice_ && !capture_notice) return S_FALSE;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    implementation_->combat_ui.draw(config_,!captures_input());
    // Full mode and the settings window are owned control surfaces, so their
    // DPI-scaled ImGui cursor is composed in the foreground after every
    // overlay window. Otherwise cursor presentation stays with the game.
    io.MouseDrawCursor = implementation_->full_input_active;
    if (captures_input()) {
        ImGui::SetNextFrameWantCaptureMouse(true);
        ImGui::SetNextFrameWantCaptureKeyboard(captures_keyboard());
    }
    if (config_.overlay_mode() == DebugOverlayMode::compact)
        draw_compact_monitor(profile, bridge, resources, implementation_->ui_scale);
    else if (config_.overlay_mode() == DebugOverlayMode::full)
        draw_full_overlay(*this, *implementation_, profile, bridge, resources, debug_views,
                          profiler, shadows);
    draw_hud_editor(*implementation_,config_);
    if (implementation_->settings_open) draw_settings_window(*implementation_, config_);
    // Critter labels remain visible after closing F10 (viewport fractions).
    if (!resources.critters.labels.empty()) {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        for (const auto& label : resources.critters.labels) {
            const ImVec2 size = ImGui::CalcTextSize(label.text);
            const ImVec2 at(label.x * io.DisplaySize.x - size.x * .5f, label.y * io.DisplaySize.y);
            draw->AddRectFilled(ImVec2(at.x - 2, at.y - 1), ImVec2(at.x + size.x + 2, at.y + size.y + 1), IM_COL32(0, 0, 0, 160));
            draw->AddText(at, IM_COL32(255, 240, 160, 255), label.text);
        }
    }
    // F11 feedback: a short progress bar while timing warms up, then a
    // confirmation, so a player's capture is never silent.
    if (capture_notice) {
        const float scale = implementation_->ui_scale;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, 12.f * scale), ImGuiCond_Always, ImVec2(.5f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(std::min(380.f * scale, io.DisplaySize.x * .9f), 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.85f);
        if (ImGui::Begin("##capture_notice", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing)) {
            if (implementation_->capture_warmup || implementation_->capture_requested) {
                ImGui::TextUnformatted(u8"\u0417\u0430\u0445\u0432\u0430\u0442 \u0434\u0438\u0430\u0433\u043d\u043e\u0441\u0442\u0438\u0447\u0435\u0441\u043a\u043e\u0433\u043e \u043a\u0430\u0434\u0440\u0430...");
                const float done = implementation_->capture_warmup ? 1.f - implementation_->capture_warmup / 90.f : 1.f;
                ImGui::ProgressBar(done, ImVec2(-FLT_MIN, 0.f), "");
            } else {
                ImGui::TextUnformatted(u8"\u041a\u0430\u0434\u0440 \u0437\u0430\u0445\u0432\u0430\u0447\u0435\u043d.");
                ImGui::TextDisabled(u8"\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043e \u0432 \u043f\u0430\u043f\u043a\u0435 \u0437\u0430\u0445\u0432\u0430\u0442\u043e\u0432 EID3D11.");
            }
        }
        ImGui::End();
    }
    if (notice_) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const float pad = 4.f * implementation_->ui_scale;
        const ImVec2 size = ImGui::CalcTextSize(notice_);
        const ImVec2 at(pad * 2.f, pad * 2.f);
        draw->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad),
                            IM_COL32(0, 0, 0, 190));
        draw->AddText(at, IM_COL32(255, 170, 60, 255), notice_);
    }
    ImGui::Render();

    ExtraShaderStageState extra_state(implementation_->context);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    extra_state.restore(implementation_->context);
    return S_OK;
}

void DebugOverlay::render_shader_preparation(std::size_t done, std::size_t total) noexcept {
    if (!implementation_ || !implementation_->imgui) return;
    ImGui::SetCurrentContext(implementation_->imgui);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    const float scale = implementation_->ui_scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(440.f * scale, io.DisplaySize.x * .9f), 0.f), ImGuiCond_Always);
    if (ImGui::Begin(u8"\u041f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043a\u0430 \u0448\u0435\u0439\u0434\u0435\u0440\u043e\u0432", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav)) {
        ImGui::TextWrapped(u8"\u041f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043a\u0430 \u0433\u0440\u0430\u0444\u0438\u043a\u0438 \u0434\u043b\u044f \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0445 \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a.");
        char label[48];
        std::snprintf(label, sizeof(label), "%zu / %zu", done, total);
        ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 1.f,
                           ImVec2(-FLT_MIN, 0.f), label);
    }
    ImGui::End();
    ImGui::Render();
    ExtraShaderStageState extra_state(implementation_->context);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    extra_state.restore(implementation_->context);
}

// F11 with the overlay hidden: frame timing is off then, so run it for
// about a second first; a player's capture must carry the same timings as a
// developer's (testers send these files).
void request_capture(DebugOverlay::Implementation& state, const RendererConfig& config) {
    if (config.overlay_mode() == DebugOverlayMode::hidden) {
        if (!state.capture_warmup && !state.capture_requested) state.capture_warmup = 90;
    } else {
        state.capture_requested = true;
    }
}

void DebugOverlay::maintain() noexcept {
    if (!implementation_) return;
    if(game_hud_editing()) {
        HudPanelBounds mission;
        if(!widescreen_hud_panel_bounds(HudPanel::messages,mission)) {
            set_game_hud_editing(false);implementation_->pending_save=true;
        }
    }
    const bool f10_down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    const bool focused = GetForegroundWindow() == implementation_->window;
    const bool shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (focused && f10_down && !implementation_->f10_was_down) {
        // Developer mode: F10 = developer cycle, Shift+F10 = player settings.
        // Players: the other way round.
        if (implementation_->developer != shift_down) {
            config_.set_overlay_mode(next_debug_overlay_mode(config_.overlay_mode()));
            implementation_->pending_save = true;
        } else if (implementation_->settings_open) {
            close_settings(*implementation_, config_, true);
        } else {
            open_settings(*implementation_, config_, false);
        }
    }
    implementation_->f10_was_down = f10_down;
    const bool f11_down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (focused && f11_down && !implementation_->f11_was_down) {
        if (shift_down) implementation_->cull_freeze_toggle = true;
        else request_capture(*implementation_, config_);
    }
    implementation_->f11_was_down = f11_down;
    if (implementation_->capture_warmup && --implementation_->capture_warmup == 0)
        implementation_->capture_requested = true;
    // Unattended measurement: EI_RENDERER_AUTO_CAPTURE=first,interval,count
    // (seconds) requests the same capture as F11 on a timer, without focus.
    {
        static const auto schedule = []() {
            struct Schedule { ULONGLONG first = 0, interval = 0; unsigned count = 0; } value{};
            char text[64]{};
            if (GetEnvironmentVariableA("EI_RENDERER_AUTO_CAPTURE", text, sizeof(text)) > 0 &&
                GetEnvironmentVariableA("EI_RENDERER_AUTO_CAPTURE", text, sizeof(text)) < sizeof(text)) {
                double first = 0, interval = 0; unsigned count = 1;
                if (std::sscanf(text, "%lf,%lf,%u", &first, &interval, &count) >= 1 && first > 0) {
                    value.first = static_cast<ULONGLONG>(first * 1000);
                    value.interval = static_cast<ULONGLONG>(interval * 1000);
                    value.count = count ? count : 1;
                }
            }
            return value;
        }();
        static const ULONGLONG start = GetTickCount64();
        static unsigned fired = 0;
        if (schedule.count && fired < schedule.count &&
            GetTickCount64() - start >= schedule.first + fired * schedule.interval) {
            ++fired;
            request_capture(*implementation_, config_);
        }
    }

    const bool full_input = focused && captures_input();
    if (implementation_->imgui) {
        ImGui::SetCurrentContext(implementation_->imgui);
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = debug_overlay_draws_software_cursor(config_.overlay_mode(), focused) ||
                             full_input;
        // The active game message loop calls its dispatcher directly, so
        // passive hover cannot rely on the HWND subclass's motion events.
        if (focused) {
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(implementation_->window, &cursor))
                io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
        } else io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);
        if (full_input) {
            for (std::size_t index = 0; index < kMouseVirtualKeys.size(); ++index) {
                const bool down = (GetAsyncKeyState(kMouseVirtualKeys[index]) & 0x8000) != 0;
                if (down != implementation_->mouse_was_down[index])
                    io.AddMouseButtonEvent(static_cast<int>(index), down);
                implementation_->mouse_was_down[index] = down;
            }
            ImGui::SetNextFrameWantCaptureMouse(true);
            ImGui::SetNextFrameWantCaptureKeyboard(captures_keyboard());
            if (GetCapture() != implementation_->window) SetCapture(implementation_->window);
        } else if (implementation_->full_input_active) {
            for (std::size_t index = 0; index < implementation_->mouse_was_down.size(); ++index) {
                if (implementation_->mouse_was_down[index]) io.AddMouseButtonEvent(static_cast<int>(index), false);
                implementation_->mouse_was_down[index] = false;
            }
            if (GetCapture() == implementation_->window) ReleaseCapture();
        }
    }
    implementation_->full_input_active = full_input;
    update_cursor_confinement(implementation_->cursor_confinement,
                              implementation_->window, focused,
                              cursor_confinement_option_enabled(config_));
    bridge_trace_input_focus(implementation_->window, "frame", 0, 0,
                              static_cast<int>(config_.overlay_mode()));
    if (!implementation_->pending_save) return;
    if (config_.save(implementation_->config_path.c_str())) {
        config_.clear_dirty();
        implementation_->pending_save = false;
    }
}

void DebugOverlay::release_confinement() noexcept {
    if (implementation_)
        release_cursor_confinement(implementation_->cursor_confinement);
}

bool DebugOverlay::wants_profiling() const noexcept {
    return implementation_ && (config_.overlay_mode() != DebugOverlayMode::hidden ||
                               implementation_->capture_warmup || implementation_->capture_requested);
}

bool DebugOverlay::visible() const noexcept {
    return mode()!=DebugOverlayMode::hidden || settings_open() ||
        game_hud_editing();
}
bool DebugOverlay::captures_keyboard() const noexcept {
    return mode()==DebugOverlayMode::full || settings_open() ||
        game_hud_editing();
}
bool DebugOverlay::captures_input() const noexcept {return captures_keyboard();}

bool DebugOverlay::hud_editing() const noexcept {return game_hud_editing();}
float DebugOverlay::hud_grid_step() const noexcept {
    return hud_editing() && implementation_->hud_grid?24.f*implementation_->ui_scale:0.f;
}

bool DebugOverlay::settings_open() const noexcept {
    return implementation_ && implementation_->settings_open;
}

bool DebugOverlay::first_run_pending() const noexcept {
    return implementation_ && !implementation_->settings_open &&
           first_run_settings_due(config_.first_run_done(), implementation_->developer);
}

void DebugOverlay::open_first_run_settings() noexcept {
    if (first_run_pending()) open_settings(*implementation_, config_, true);
}

bool DebugOverlay::consume_cull_freeze_toggle() noexcept {
    if (!implementation_) return false;
    const bool requested = implementation_->cull_freeze_toggle;
    implementation_->cull_freeze_toggle = false;
    return requested;
}

bool DebugOverlay::consume_capture_request() noexcept {
    if (!implementation_) return false;
    const bool requested = implementation_->capture_requested;
    implementation_->capture_requested = false;
    if (requested) implementation_->capture_notice_until = GetTickCount64() + 3000;
    return requested;
}

bool DebugOverlay::consume_display_apply(DisplaySettings& settings) noexcept {
    if (!implementation_ || !implementation_->display_apply_pending) return false;
    settings = implementation_->display_apply;
    implementation_->display_apply_pending = false;
    return true;
}
void DebugOverlay::finish_display_apply(const DisplaySettings& settings, HRESULT result, const char* message) noexcept {
    if (!implementation_) return;
    auto& state = *implementation_;
    std::snprintf(state.display_apply_result, sizeof(state.display_apply_result), "%s", message ? message : "");
    if (FAILED(result)) return;
    state.running_display = {int(settings.mode), settings.window_width, settings.window_height};
    // Cancel may revert other unapplied options, but must not restore display
    // controls to the dimensions preceding a completed live Apply.
    const auto& options = config_.options();
    for (std::size_t i = 0; i < state.settings_snapshot.size() && i < options.size(); ++i) {
        if (options[i].key == "display_mode") state.settings_snapshot[i].integer = int(settings.mode);
        else if (options[i].key == "window_width") state.settings_snapshot[i].integer = settings.window_width;
        else if (options[i].key == "window_height") state.settings_snapshot[i].integer = settings.window_height;
    }
    state.pending_save = true;
}

void DebugOverlay::set_mode(DebugOverlayMode mode) noexcept {
    config_.set_overlay_mode(mode);
    if (implementation_) implementation_->pending_save = true;
}

bool DebugOverlay::initialized() const noexcept {
    return implementation_ && implementation_->imgui;
}

} // namespace owned
