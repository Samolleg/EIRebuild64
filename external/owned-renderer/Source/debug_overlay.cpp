#include "test_harness.h"
#include "debug_overlay.h"
#include "combat_ui.h"

#include "cursor_confinement.h"
#include "cursor_image.h"
#include "game_focus.h"
#include "render_profiler.h"
#include "engine_bridge.h"
#include "game_sites.h"
#include "graphics_settings.h"
#include "ui_language.h"
#include "widescreen.h"
#include "world_fog.h"
#include "camp_inventory.h"
#include "x64_remote_link.h"
#include "dlss_frame.h"

#include <dxgi1_4.h>

#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_dx11.h"
#include "../ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <vector>

#include <psapi.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM wparam, LPARAM lparam);

namespace owned {
void address_space_mb(std::uint64_t& used, std::uint64_t& largest_free, bool walk) noexcept; // renderer_diagnostics.cpp
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

// The renderer this session runs (debug_overlay_set_running_renderer).
const char* g_running_renderer = nullptr;
bool g_running_core = false;
bool g_running_d3d12 = false;
// Core mode, host: the game process's address space (MB) from the control block.
std::atomic<std::uint32_t> g_core_game_va_used_mb{0}, g_core_game_va_free_mb{0};

// Renderer selection. Embedded retains unavailable DLSS and refuses world
// frames; the menu must describe that choice rather than calling it Off.
int active_upscaler(const RendererConfig& config) {
    const int method = configured_value(config, "upscaler");
#ifdef EID3D11_EMBEDDED
    if (method == kUpscalerDlss) return kUpscalerDlss; // retained selection; failed world frames are refused
#else
    if (method == kUpscalerDlss && !dlss_unavailable_reason()) return kUpscalerDlss;
#endif
    if (method == kUpscalerFsr && !fsr_unavailable_reason()) return kUpscalerFsr;
    return kUpscalerOff;
}

// An option that has no effect under the current value of the one it
// depends on: drawn greyed with a note, its own value kept for later.
const char* blocked_option_note(const RendererConfig& config, const char* key) {
#ifdef EID3D11_EMBEDDED
    if (std::strcmp(key, "render_thread") == 0 || std::strcmp(key, "render_thread_frame_latency") == 0)
        return "Scene thread is not yet available in EIRebuild";
#endif
    // Terrain cliffs need the material blend (configured_terrain_cliffs).
    if (std::strcmp(key, "terrain_cliffs") == 0 && configured_value(config, "terrain_blend") <= 0)
        return "needs material blend";
    // The portrait helmet belongs to the owned portrait head (renderer_game.cpp, default on).
    if (std::strcmp(key, "portrait_helmet") == 0) {
        const RendererOption* head = config.find("portrait_head");
        if (head && head->type == RendererOptionType::boolean && !head->boolean_value) return "needs real head portraits";
    }
    // The 64-bit render host cannot run on 32-bit Windows or without its exe.
    if (std::strcmp(key, "x64_render_host") == 0) return x64remote::host_unavailable_reason();
    // Direct3D 12 runs in the 64-bit render host.
    if (std::strcmp(key, "renderer_backend") == 0) {
        if (const char* reason = x64remote::host_unavailable_reason()) return reason;
        const RendererOption* host = config.find("x64_render_host");
        if (host && host->type == RendererOptionType::boolean && !host->boolean_value) return "needs 64-bit render host";
    }
    // Frame generation, its floor, Reflex and latency pacing exist on the
    // Direct3D 12 backend only (a changed Renderer starts after a restart).
    if (!g_running_d3d12 && (std::strcmp(key, "frame_generation") == 0 || std::strcmp(key, "frame_generation_min_fps") == 0 ||
                             std::strcmp(key, "reflex") == 0 || std::strcmp(key, "latency_pacing") == 0))
        return configured_value(config, "renderer_backend") != 1 ? "needs Direct3D 12 renderer" :
               g_running_renderer && std::strstr(g_running_renderer, "Direct3D 12 refused") ? "Direct3D 12 could not start (Direct3D 11 runs)" :
               "Direct3D 12 starts after a restart";
    // The Upscaling method (dlss_frame.h): DLSS runs in the 64-bit host on an
    // RTX card, FSR on any Direct3D 11.3 card; greyed only when neither can.
    // A saved unavailable DLSS must still be changeable to Off in this menu.
    if (std::strcmp(key, "upscaler") == 0 && dlss_unavailable_reason() && fsr_unavailable_reason()
        && configured_value(config, "upscaler") != kUpscalerDlss)
        return "no upscaler available";
    const int upscaler = active_upscaler(config);
    if (upscaler == kUpscalerDlss && dlss_unavailable_reason() &&
        (std::strcmp(key, "upscaler_quality") == 0 || std::strcmp(key, "upscale_sharpness") == 0)) return dlss_unavailable_reason();
    // Frame generation (dlss_frame.h): DLSS-G on the D3D12 backend of the
    // host; it takes the upscaler's depth and motion vectors.
    if (std::strcmp(key, "frame_generation") == 0) {
        if (const char* reason = frame_generation_unavailable_reason()) return reason;
        if (upscaler == kUpscalerOff) return "needs DLSS or FSR upscaling (motion vectors)";
    }
    // Quality and sharpening belong to a running upscaler.
    if ((std::strcmp(key, "upscaler_quality") == 0 || std::strcmp(key, "upscale_sharpness") == 0) &&
        upscaler == kUpscalerOff)
        return "needs upscaling";
    // An active upscaler replaces SMAA, and its quality sets the render size.
    if (std::strcmp(key, "smaa") == 0 || std::strcmp(key, "render_scale") == 0) {
        if (upscaler == kUpscalerDlss) return "replaced by DLSS";
        if (upscaler == kUpscalerFsr) return "replaced by FSR";
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

// Segoe UI (Tahoma on older Windows) carries Cyrillic; empty when neither exists.
std::string system_ui_font_path() {
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    for (const wchar_t* name : {L"\\Fonts\\segoeui.ttf", L"\\Fonts\\tahoma.ttf"}) {
        const std::wstring path = std::wstring(directory) + name;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        char utf8[MAX_PATH * 3]{};
        if (WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr) > 0) return utf8;
    }
    return {};
}
// Segoe UI fits ascent plus descent (1.33 em) into the line height; this
// brings its em close to the built-in 13 px font.
constexpr float kCyrillicGlyphScale = 1.2f;

// Translated visible label with a fixed ImGui ID, so a window, tab or
// header keeps its position and open state across a language switch.
std::string window_title(const char* english, const char* id) {
    return std::string(ui_text(english)) + "###" + id;
}

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

// Over-budget marker: the game's orderly quit is WM_QUIT reaching its main
// loop (NGame::QuitProgram posts it; WinMain then releases its textures,
// interface and visualizator, and only after that the DirectDraw object).
// A WH_GETMESSAGE hook on the window's thread, installed only while the
// marker is set, clears it there, before any teardown Release can hang.
HHOOK g_quit_hook = nullptr;
std::wstring g_quit_hook_ini;
std::atomic<bool> g_quit_seen{false};
LRESULT CALLBACK quit_message_hook(int code, WPARAM wparam, LPARAM lparam) {
    const MSG* message = reinterpret_cast<const MSG*>(lparam);
    if (code == HC_ACTION && message && message->message == WM_QUIT && !g_quit_seen.exchange(true)) {
        WritePrivateProfileStringW(L"setup", L"over_budget_pending", nullptr, g_quit_hook_ini.c_str());
        bridge_report_event("vram_budget_marker_cleared", "WM_QUIT: orderly quit after an over-budget save");
    }
    return CallNextHookEx(g_quit_hook, code, wparam, lparam);
}
void install_quit_hook(HWND window, const std::wstring& ini) {
    if (g_quit_hook || !window) return;
    try { g_quit_hook_ini = ini; } catch (...) { return; }
    g_quit_hook = SetWindowsHookExW(WH_GETMESSAGE, quit_message_hook, nullptr,
                                    GetWindowThreadProcessId(window, nullptr));
}
void remove_quit_hook() {
    if (g_quit_hook) UnhookWindowsHookEx(g_quit_hook);
    g_quit_hook = nullptr;
}

} // namespace

struct DebugOverlay::Implementation {
    CombatUiRenderer combat_ui;
    HWND window = nullptr;
    // Core mode: host_child (the host's overlay: no hook, focus from the
    // game), core_input (the game process: hook only, host capture flags).
    bool host_child = false;
    bool external_focused = false;
    DebugOverlay::CoreInputHooks core_input{};
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
    ULONGLONG capture_notice_until = 0;  // "Frame captured" notice, tick ms
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
    // VRAM budget: the save warning (MiB of the estimate and the budget) and
    // the one-time notice after an over-budget session ended uncleanly.
    bool vram_warning_open = false;
    double vram_warning_need_mib = 0.0, vram_warning_budget_mib = 0.0;
    bool graphics_reset_pending = false;
    ULONGLONG graphics_reset_until = 0;
    char graphics_reset_text[1024] = "";
    bool hud_grid = false;
    char profile_name[64] = "";
    bool panel_resizing = false;
    ImVec2 panel_drag_start{}, panel_size_start{};
    ImGuiTextFilter settings_filter;
    int samgbur_lod_instance = 0;
    ImGuiTextFilter graphics_filter;
    ImGuiTextFilter diagnostics_filter;
    // The running game.exe has the 4 GB (large-address-aware) flag; else the
    // settings offer to patch it, and this holds the patch result.
    bool large_address_aware = true;
    const char* large_address_result = nullptr;
    int preset_slider = static_cast<int>(GraphicsPreset::high);
    GraphicsPreset suggested_preset = GraphicsPreset::custom;
    char adapter_name[128] = "";
    std::uint64_t adapter_budget_bytes = 0;  // WDDM local budget at startup
    struct SavedValue { bool boolean; int integer; float floating; std::string text; };
    std::vector<SavedValue> settings_snapshot;  // option values (registration order) restored by Cancel
    std::array<int, 3> running_display{};  // last successfully applied settings
    bool display_apply_pending = false;
    DisplaySettings display_apply{};
    char display_apply_result[256] = "";
    std::vector<std::pair<int, int>> window_sizes;
    // Menu fonts: the built-in Latin font (with Cyrillic merged in for game
    // text) and a Windows UI font drawn as the default in Russian.
    ImFont* latin_font = nullptr;
    ImFont* cyrillic_font = nullptr;

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

std::recursive_mutex& debug_overlay_lock() noexcept {
    static std::recursive_mutex lock;
    return lock;
}

namespace {

LRESULT CALLBACK overlay_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                       UINT_PTR, DWORD_PTR reference) {
    auto* overlay = reinterpret_cast<DebugOverlay*>(reference);
    if (!overlay || !overlay->input_ready()) {
        camp_inventory_cancel_drag();return DefSubclassProc(window, message, wparam, lparam);
    }
    const auto forward = overlay->core_forward();
    // Hover-only combat UI observes motion while gameplay keeps owning input.
    // Keep the backend's tracked position current after F10 has closed.
    if (!forward && !overlay->captures_input() && (message==WM_MOUSEMOVE || message==WM_MOUSELEAVE ||
        message==WM_NCMOUSEMOVE || message==WM_NCMOUSELEAVE)) {
        std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
        ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);
    }
    // Camp rail ownership must end before focus swallowing or renderer controls
    // can hide a release. No native item, rotation or keyboard route is replaced.
    if(overlay->process_camp_input(message,wparam,lparam))return 0;
    if (forward) {
        // Core mode: the overlay and the combat UI run on the host. The wheel
        // always goes there (the pane's effect list scrolls; the game still
        // gets it unless the overlay takes input); keys and characters while
        // the overlay takes input.
        const bool wheel = message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL;
        if (wheel || (overlay->captures_input() && (is_keyboard_message(message) || message==WM_CHAR || message==WM_UNICHAR))) {
            // The host's child window is Unicode: an ANSI game window's
            // character arrives in the game's code page.
            WPARAM forwarded = wparam;
            if (message == WM_CHAR && !IsWindowUnicode(window)) {
                const char byte = static_cast<char>(wparam & 0xff);
                wchar_t wide = 0;
                if (MultiByteToWideChar(CP_ACP, 0, &byte, 1, &wide, 1) == 1) forwarded = wide;
            }
            forward(message, forwarded, lparam);
        }
    } else {
        std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
        if(overlay->process_combat_input(message,wparam,lparam))return 0;
    }

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
        LRESULT handled = 0;
        if (!forward) {
            std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
            handled = ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        }
        // Full mode and the settings window are explicit renderer-control
        // surfaces. Always consume their
        // keyboard and pointer messages instead of relying on the previous
        // frame's WantCapture flags, which leaves the first click exposed to
        // the legacy input path.
        if ((is_keyboard_message(message) && overlay->captures_keyboard()) || is_mouse_message(message))
            return handled ? handled : 1;
    }
    // The held loading picture shows the game cursor as the system cursor
    // (cursor_image.h); the game's window procedure would hide it here.
    if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT && apply_load_cursor()) return TRUE;
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (focus_message)
        bridge_trace_input_focus(window, "after_game", message, wparam,
                                  static_cast<int>(overlay->mode()));
    return result;
}

void draw_live_statistics(const RendererResourceSnapshot& resources) {
    if (resources.rolling_frame_samples) {
        ImGui::Text(ui_text("5s avg %.1f FPS  5s low %.1f FPS"), resources.rolling_average_fps,
                    resources.rolling_lowest_fps);
    } else {
        ImGui::TextUnformatted(ui_text("5s avg n/a  5s low n/a"));
    }
    if (resources.culling_frozen)
        ImGui::TextColored(ImVec4(1.f, .35f, .2f, 1.f), ui_text("CULLING FROZEN (Shift+F11): objects culled for the frozen camera"));
    if (resources.fog_status[0])
        ImGui::TextColored(resources.fog_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.fog_status);
    if (resources.sky_status[0])
        ImGui::TextColored(resources.sky_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.sky_status);
    if (resources.particle_status[0])
        ImGui::TextColored(resources.particle_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.particle_status);
    if (resources.precipitation_status[0])
        ImGui::TextColored(resources.precipitation_problem ? ImVec4(1.f, .35f, .2f, 1.f) : ImVec4(.75f, .9f, 1.f, 1.f), "%s",
                           resources.precipitation_status);
    if (resources.critter_status[0]) ImGui::TextUnformatted(resources.critter_status);
    if (resources.emissive_status[0]) ImGui::TextColored(ImVec4(1.f, .62f, .3f, 1.f), "%s", resources.emissive_status);
    ImGui::Text(ui_text("Shadows %s  depth draws %llu  triangles %llu"),
                ui_text(resources.native_shadows_enabled ? "ON" : "OFF"),
                static_cast<unsigned long long>(resources.native_shadow_draws),
                static_cast<unsigned long long>(resources.native_shadow_triangles));
    const auto& point=resources.point_shadows;
    ImGui::Text(ui_text("Point lamps: baked %llu  rebaked %llu  overlays %llu  budget skipped %llu"),
        point.baked,point.rebaked,point.overlays,point.budget);
    ImGui::Text(ui_text("Point invalidation: radius %llu  terrain %llu  light %llu  list %llu  reset %llu"),
        point.radius_dirty,point.terrain,point.light,point.list,point.reset);
    ImGui::Text(ui_text("Point capacity: allocation failures %llu  pressure retirements %llu"),
        point.allocation_failures,point.pressure_drops);
    if (resources.rolling_frame_capacity_truncated) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.18f, 1.0f));
        ImGui::TextUnformatted(ui_text("5s window truncated: frame-rate exceeds sample capacity"));
        ImGui::PopStyleColor();
    } else if (resources.rolling_frame_warming_up) {
        ImGui::Text(ui_text("5s window warming: %.1f / 5.0 s"), resources.rolling_frame_seconds);
    }

    if (resources.system_telemetry_stale) {
        if (resources.system_telemetry_age_ms)
            ImGui::Text(ui_text("Game CPU n/a  GPU 3D n/a  (telemetry stale: %llu ms)"),
                        static_cast<unsigned long long>(resources.system_telemetry_age_ms));
        else
            ImGui::TextUnformatted(ui_text("Game CPU n/a  GPU 3D n/a  (telemetry warming up)"));
    } else if (resources.process_cpu_valid && resources.gpu_3d_percent_valid) {
        ImGui::Text(ui_text("Game CPU %.1f%%  GPU 3D %.1f%%"), resources.process_cpu_percent,
                    resources.gpu_3d_percent);
    } else if (resources.process_cpu_valid) {
        ImGui::Text(ui_text("Game CPU %.1f%%  GPU 3D n/a"), resources.process_cpu_percent);
    } else if (resources.gpu_3d_percent_valid) {
        ImGui::Text(ui_text("Game CPU n/a  GPU 3D %.1f%%"), resources.gpu_3d_percent);
    } else {
        ImGui::TextUnformatted(ui_text("Game CPU n/a  GPU 3D n/a"));
    }

    if (!resources.system_telemetry_stale && resources.process_private_bytes_valid &&
        resources.gpu_memory_valid) {
        ImGui::Text(ui_text("RAM private %.1f MiB  VRAM local %.1f / %.1f MiB"),
                    mib(resources.process_private_bytes), mib(resources.gpu_usage_bytes),
                    mib(resources.gpu_budget_bytes));
    } else if (!resources.system_telemetry_stale && resources.process_private_bytes_valid) {
        ImGui::Text(ui_text("RAM private %.1f MiB  VRAM local n/a"),
                    mib(resources.process_private_bytes));
    } else if (!resources.system_telemetry_stale && resources.gpu_memory_valid) {
        ImGui::Text(ui_text("RAM private n/a  VRAM local %.1f / %.1f MiB"),
                    mib(resources.gpu_usage_bytes), mib(resources.gpu_budget_bytes));
    } else {
        ImGui::TextUnformatted(ui_text("RAM private n/a  VRAM local n/a"));
    }
    const VramBudgetSnapshot& vram = resources.vram_budget;
    if (vram.known && vram.enforced)
        ImGui::Text(ui_text("VRAM budget: %.0f/%.0f MiB%s, reduced: %s, idle evicted %.0f MiB"), mib(vram.usage_bytes),
                    mib(vram.budget_bytes), vram.overridden ? ui_text(" (override)") : "",
                    ui_text(vram_budget_level_text(vram.level)), mib(vram.evicted_bytes));
    else if (vram.known && vram.uma)
        ImGui::Text(ui_text("VRAM budget: %.0f/%.0f MiB shared memory (UMA), not enforced"), mib(vram.usage_bytes),
                    mib(vram.budget_bytes));
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
        ImGui::TextUnformatted(ui_text("EID3D11  |  profiler warming up..."));
    } else {
        const double interval_ms = resources.present_smoothed_ms > 0.0
            ? resources.present_smoothed_ms
            : (bridge && bridge->begin_interval_ms > 0.0
                ? bridge->begin_interval_ms : 0.0);
        const double fps = interval_ms > 0.0 ? 1000.0 / interval_ms : 0.0;
        if (profile->gpu_valid)
            ImGui::Text(ui_text("EID3D11  %.0f FPS / %.2f ms  Render CPU %.2f ms  GPU %.2f ms"),
                        fps, interval_ms, profile->frame_cpu_ms, profile->frame_gpu_ms);
        else
            ImGui::Text(ui_text("EID3D11  %.0f FPS / %.2f ms  Render CPU %.2f ms  GPU n/a"),
                        fps, interval_ms, profile->frame_cpu_ms);
        if (resources.fg_presented_fps > 0.0 && resources.fg_mode == 2)
            ImGui::Text(ui_text("FSR frame generation: presented %.0f FPS, of them real %.0f; rendered %.0f FPS (GPU excludes FidelityFX's present queue)"),
                        resources.fg_presented_fps, resources.fg_real_fps, fps);
        else if (resources.fg_presented_fps > 0.0)
            ImGui::Text(ui_text("Frame generation (DLSS-G): presented %.0f FPS, of them real %.0f; rendered %.0f FPS"),
                        resources.fg_presented_fps, resources.fg_real_fps, fps);
        if (resources.fg_mode && resources.fg_auto_off_fps)
            ImGui::Text(ui_text("Frame generation paused: rendered rate below %d FPS (frame_generation_min_fps)"), resources.fg_auto_off_fps);
        std::uint64_t draws = 0, triangles = 0, batches = 0;
        for (UINT index = 0; index < profile->pass_count && index < profile->passes.size(); ++index) {
            draws += profile->passes[index].counters.draws;
            triangles += profile->passes[index].counters.triangles;
            batches += profile->passes[index].counters.batches;
        }
        ImGui::Text(ui_text("Draws %llu  Triangles %llu  Batches %llu"),
                    static_cast<unsigned long long>(draws),
                    static_cast<unsigned long long>(triangles),
                    static_cast<unsigned long long>(batches));
        ImGui::Text(ui_text("Renderer tracked %.0f MiB"), mib(resources.renderer_resident_bytes));
        if (bridge) {
            ImGui::Text(ui_text("API %.2f ms  Outside %.2f ms"), bridge->frame_cpu_ms,
                        bridge->outside_entry_cpu_ms);
            ImGui::Text(ui_text("3D scene %.2f ms  unexplained %.2f ms  other %.2f ms"),
                        bridge->scene_3d_cpu_ms,
                        bridge->scene_3d_unattributed_cpu_ms,
                        bridge->outside_scene_3d_cpu_ms);
            ImGui::Text(ui_text("Simple %.2f ms / %llu  Figures %.2f ms / %llu"),
                        bridge->simple_cpu_ms,
                        static_cast<unsigned long long>(bridge->simple_calls),
                        bridge->figure_placed_cpu_ms + bridge->figure_lit_cpu_ms,
                        static_cast<unsigned long long>(bridge->figure_placed_calls +
                                                        bridge->figure_lit_calls));
            ImGui::Text(ui_text("GPU figures %llu  CPU fallback %llu"),
                        static_cast<unsigned long long>(bridge->figure_gpu_owned_calls),
                        static_cast<unsigned long long>(bridge->figure_cpu_fallback_calls));
            ImGui::Text(ui_text("Terrain %.2f ms / %llu  GPU %llu  fallback %llu"),
                        bridge->terrain_cpu_ms,
                        static_cast<unsigned long long>(bridge->terrain_calls),
                        static_cast<unsigned long long>(bridge->terrain_gpu_owned_calls),
                        static_cast<unsigned long long>(bridge->terrain_cpu_fallback_calls));
            ImGui::Text(ui_text("Terrain outer %.2f ms  capture %.2f ms  submit %.2f ms"),
                        bridge->terrain_outer_cpu_ms, bridge->terrain_capture_cpu_ms,
                        bridge->terrain_submit_cpu_ms);
            ImGui::Text(ui_text("Owned prep/reuse/omit/original: %llu / %llu / %llu / %llu"),
                        static_cast<unsigned long long>(bridge->terrain_owned_frame_preparations),
                        static_cast<unsigned long long>(bridge->terrain_owned_transparent_reuses),
                        static_cast<unsigned long long>(bridge->terrain_owned_omitted_ranges),
                        static_cast<unsigned long long>(
                            bridge->terrain_original_render_front_end_calls));
            ImGui::Text(ui_text("Sectors candidate/visible/frustum: %llu / %llu / %llu"),
                        static_cast<unsigned long long>(bridge->terrain_candidate_sectors),
                        static_cast<unsigned long long>(bridge->terrain_visible_sectors),
                        static_cast<unsigned long long>(bridge->terrain_frustum_rejected_sectors));
            ImGui::Text(ui_text("GPU anim %llu  waves %llu  tiles %llu  fallback %llu"),
                        static_cast<unsigned long long>(bridge->terrain_animation_owned_updates),
                        static_cast<unsigned long long>(bridge->terrain_wave_sectors),
                        static_cast<unsigned long long>(bridge->terrain_animated_tile_updates),
                        static_cast<unsigned long long>(bridge->terrain_animation_fallback_updates));
            std::uint64_t dominant_count = 0;
            const auto dominant = dominant_figure_fallback(*bridge, dominant_count);
            if (dominant_count || bridge->figure_gpu_submit_failures ||
                bridge->figure_gpu_culled_calls)
                ImGui::Text(ui_text("Fallback: %s %llu  Submit fail %llu  Culled %llu"),
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
        ImGui::TextUnformatted(ui_text("Profiler is warming up. GPU timestamp results are delayed and non-blocking."));
        return;
    }
    const double frame_ms = resources.present_smoothed_ms;
    const double fps = frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0;
    ImGui::Text(ui_text("Frame %llu  |  Present %.1f FPS / %.3f ms  |  Renderer CPU %.3f ms"),
                static_cast<unsigned long long>(profile->frame), fps, frame_ms,
                profile->frame_cpu_ms);
    ImGui::SameLine();
    if (profile->gpu_valid) ImGui::Text(ui_text("|  GPU %.3f ms"), profile->frame_gpu_ms);
    else ImGui::TextUnformatted(ui_text("|  GPU unavailable"));
    if (resources.fg_presented_fps > 0.0 && resources.fg_mode == 2)
        ImGui::Text(ui_text("FSR frame generation: presented %.1f FPS, of them real %.1f (rendered %.1f; GPU excludes FidelityFX's present queue)"),
                    resources.fg_presented_fps, resources.fg_real_fps, fps);
    else if (resources.fg_presented_fps > 0.0)
        ImGui::Text(ui_text("Frame generation (DLSS-G): presented %.1f FPS, of them real %.1f (rendered %.1f)"),
                    resources.fg_presented_fps, resources.fg_real_fps, fps);
    if (resources.fg_mode && resources.fg_auto_off_fps)
        ImGui::Text(ui_text("Frame generation paused: rendered rate below %d FPS (frame_generation_min_fps)"), resources.fg_auto_off_fps);

    std::array<float, kFrameHistorySize> cpu{};
    std::array<float, kFrameHistorySize> gpu{};
    std::array<float, kFrameHistorySize> present{};
    state.linear_history(state.cpu_history, cpu);
    state.linear_history(state.gpu_history, gpu);
    state.linear_history(state.present_history, present);
    const int count = static_cast<int>(state.history_count);
    ImGui::PlotLines(ui_text("Present cadence (ms)"), present.data(), count, 0, nullptr, 0.0f, 33.4f,
                     ImVec2(-1.0f, 72.0f * state.ui_scale));
    ImGui::PlotLines(ui_text("Renderer CPU work (ms)"), cpu.data(), count, 0, nullptr, 0.0f, 33.4f,
                     ImVec2(-1.0f, 72.0f * state.ui_scale));
    if (profile->gpu_valid)
        ImGui::PlotLines(ui_text("GPU frame (ms)"), gpu.data(), count, 0, nullptr, 0.0f, 33.4f,
                         ImVec2(-1.0f, 72.0f * state.ui_scale));

    if (ImGui::BeginTable("Passes", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                           ImVec2(0.0f, 270.0f * state.ui_scale))) {
        ImGui::TableSetupColumn(ui_text("Pass"));
        ImGui::TableSetupColumn(ui_text("CPU ms"));
        ImGui::TableSetupColumn(ui_text("GPU ms"));
        ImGui::TableSetupColumn(ui_text("Draws"));
        ImGui::TableSetupColumn(ui_text("Triangles"));
        ImGui::TableSetupColumn(ui_text("Batches"));
        ImGui::TableSetupColumn(ui_text("Uploads"));
        ImGui::TableSetupColumn(ui_text("Upload MiB"));
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

    ImGui::SeparatorText(ui_text("Memory"));
    if (resources.gpu_memory_valid) {
        ImGui::Text(ui_text("WDDM local budget: %.1f MiB"), mib(resources.gpu_budget_bytes));
        ImGui::Text(ui_text("Process local usage: %.1f MiB"), mib(resources.gpu_usage_bytes));
    } else {
        ImGui::TextUnformatted(ui_text("WDDM budget telemetry unavailable"));
    }
    ImGui::Text(ui_text("Renderer tracked residency: %.1f MiB"), mib(resources.renderer_resident_bytes));
    ImGui::Text(ui_text("Transient upload storage: %.1f MiB"), mib(resources.transient_upload_bytes));
    ImGui::Text(ui_text("CPU surface storage: %.1f MiB"), mib(resources.cpu_surface_bytes));
    ImGui::Text(ui_text("GPU texture storage: %.1f MiB"), mib(resources.gpu_texture_bytes));
    if (resources.double_resident_bytes) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.18f, 1.0f));
        ImGui::Text(ui_text("Persistent CPU + GPU duplicates: %.1f MiB"), mib(resources.double_resident_bytes));
        ImGui::PopStyleColor();
    }
    ImGui::Text(ui_text("GPU-only surfaces: %llu (%.1f MiB CPU freed), pinned %llu"),
                static_cast<unsigned long long>(resources.released_surfaces),
                mib(resources.released_surface_bytes),
                static_cast<unsigned long long>(resources.pinned_surfaces));
    ImGui::Text(ui_text("Surface restore readbacks: %llu (%.2f ms total)"),
                static_cast<unsigned long long>(resources.surface_restore_readbacks),
                resources.surface_restore_readback_ms);

    ImGui::SeparatorText(ui_text("Native batching"));
    ImGui::Text(ui_text("Logical draws: %llu  Physical batches: %llu"),
                static_cast<unsigned long long>(resources.native_logical_draws),
                static_cast<unsigned long long>(resources.native_physical_batches));
    ImGui::Text(ui_text("Reorderable: %llu  Ordered: %llu"),
                static_cast<unsigned long long>(resources.native_reorderable_draws),
                static_cast<unsigned long long>(resources.native_ordered_draws));
    ImGui::Text(ui_text("Opaque: %llu  Cutout: %llu  Material buckets: %llu"),
                static_cast<unsigned long long>(resources.native_opaque_draws),
                static_cast<unsigned long long>(resources.native_cutout_draws),
                static_cast<unsigned long long>(resources.native_material_buckets));
    ImGui::Text(ui_text("Visibility dispatches: %llu  One-draw batches: %llu"),
                static_cast<unsigned long long>(resources.native_compute_dispatches),
                static_cast<unsigned long long>(resources.native_one_draw_batches));
    ImGui::Text(ui_text("Largest batch: %llu draws"),
                static_cast<unsigned long long>(resources.native_max_batch_draws));
    ImGui::Text(ui_text("Terrain texture arrays: %llu / %llu successes"),
                static_cast<unsigned long long>(resources.native_texture_array_successes),
                static_cast<unsigned long long>(resources.native_texture_array_attempts));
    ImGui::Text(ui_text("Array fallback unavailable/format/layout/alloc: %llu / %llu / %llu / %llu"),
                static_cast<unsigned long long>(resources.native_texture_array_fallback_unavailable),
                static_cast<unsigned long long>(resources.native_texture_array_fallback_format),
                static_cast<unsigned long long>(resources.native_texture_array_invalid_layout),
                static_cast<unsigned long long>(resources.native_texture_array_allocation_failures));
    ImGui::Text(ui_text("Array residency classes/pages/slices: %llu / %llu / %llu"),
                static_cast<unsigned long long>(resources.native_texture_array_classes),
                static_cast<unsigned long long>(resources.native_texture_array_pages),
                static_cast<unsigned long long>(resources.native_texture_array_slices));
    ImGui::Text(ui_text("Terrain contract reorder/preserve: %llu / %llu"),
                static_cast<unsigned long long>(resources.native_terrain_reorderable_contracts),
                static_cast<unsigned long long>(resources.native_terrain_preserve_contracts));
    ImGui::Text(ui_text("Terrain stage-0 textured/untextured: %llu / %llu"),
                static_cast<unsigned long long>(resources.native_terrain_textured_draws),
                static_cast<unsigned long long>(resources.native_terrain_untextured_draws));
    ImGui::Text(ui_text("Terrain topology uploads: %llu / %llu vertices  Color-only: %llu / %llu vertices"),
                static_cast<unsigned long long>(resources.native_terrain_topology_uploads),
                static_cast<unsigned long long>(resources.native_terrain_topology_upload_vertices),
                static_cast<unsigned long long>(resources.native_terrain_color_only_uploads),
                static_cast<unsigned long long>(resources.native_terrain_color_only_upload_vertices));

    if (bridge) {
        ImGui::SeparatorText(ui_text("Engine / API CPU"));
        ImGui::Text(ui_text("Engine scene-begin cadence: %.3f ms"), bridge->begin_interval_ms);
        ImGui::Text(ui_text("Measured frame CPU: %.3f ms  Outside entry points: %.3f ms"),
                    bridge->frame_cpu_ms, bridge->outside_entry_cpu_ms);
        ImGui::Text(ui_text("World draw: %.3f ms / %llu  unattributed: %.3f ms"),
                    bridge->world_draw_cpu_ms,
                    static_cast<unsigned long long>(bridge->world_draw_calls),
                    bridge->world_unattributed_cpu_ms);
        ImGui::Text(ui_text("3D scene: %.3f ms / %llu  unattributed: %.3f ms  outside 3D: %.3f ms"),
                    bridge->scene_3d_cpu_ms,
                    static_cast<unsigned long long>(bridge->scene_3d_calls),
                    bridge->scene_3d_unattributed_cpu_ms,
                    bridge->outside_scene_3d_cpu_ms);
        ImGui::Text(ui_text("3D objects: %.3f ms / %llu  object-local residual: %.3f ms"),
                    bridge->object_draw_cpu_ms,
                    static_cast<unsigned long long>(bridge->object_draw_calls),
                    bridge->object_draw_unattributed_cpu_ms);
        ImGui::Text(ui_text("Simple geometry: %.3f ms / %llu calls"), bridge->simple_cpu_ms,
                    static_cast<unsigned long long>(bridge->simple_calls));
        ImGui::Text(ui_text("Terrain ingress: %.3f ms / %llu calls"), bridge->terrain_cpu_ms,
                    static_cast<unsigned long long>(bridge->terrain_calls));
        ImGui::Text(ui_text("Terrain range: %.3f ms / %llu calls  Capture: %.3f ms  Submit: %.3f ms"),
                    bridge->terrain_outer_cpu_ms,
                    static_cast<unsigned long long>(bridge->terrain_outer_calls),
                    bridge->terrain_capture_cpu_ms, bridge->terrain_submit_cpu_ms);
        ImGui::Text(ui_text("Owned terrain prep/reuse/omitted/original front-end: %llu / %llu / %llu / %llu"),
                    static_cast<unsigned long long>(bridge->terrain_owned_frame_preparations),
                    static_cast<unsigned long long>(bridge->terrain_owned_transparent_reuses),
                    static_cast<unsigned long long>(bridge->terrain_owned_omitted_ranges),
                    static_cast<unsigned long long>(
                        bridge->terrain_original_render_front_end_calls));
        ImGui::Text(ui_text("Terrain game publication: %llu sectors  Dynamic colors: %llu vertices"),
                    static_cast<unsigned long long>(bridge->terrain_game_publication_sectors),
                    static_cast<unsigned long long>(
                        bridge->terrain_dynamic_color_vertices_copied));
        ImGui::Text(ui_text("Terrain structural copies: %llu vertices  Source topology: %llu / %llu vertices"),
                    static_cast<unsigned long long>(
                        bridge->terrain_structural_vertices_copied),
                    static_cast<unsigned long long>(bridge->terrain_source_topology_decodes),
                    static_cast<unsigned long long>(bridge->terrain_source_topology_vertices));
        ImGui::Text(ui_text("Terrain color-only decodes: %llu / %llu vertices"),
                    static_cast<unsigned long long>(bridge->terrain_source_color_decodes),
                    static_cast<unsigned long long>(bridge->terrain_source_color_vertices));
        ImGui::Text(ui_text("GPU-owned terrain: %llu  CPU fallbacks: %llu  Submit failures: %llu"),
                    static_cast<unsigned long long>(bridge->terrain_gpu_owned_calls),
                    static_cast<unsigned long long>(bridge->terrain_cpu_fallback_calls),
                    static_cast<unsigned long long>(bridge->terrain_gpu_submit_failures));
        ImGui::Text(ui_text("Terrain sectors candidate/visible/frustum rejected: %llu / %llu / %llu"),
                    static_cast<unsigned long long>(bridge->terrain_candidate_sectors),
                    static_cast<unsigned long long>(bridge->terrain_visible_sectors),
                    static_cast<unsigned long long>(bridge->terrain_frustum_rejected_sectors));
        ImGui::Text(ui_text("Terrain animation GPU/CPU fallback: %llu / %llu  wave sectors: %llu  tile phases: %llu"),
                    static_cast<unsigned long long>(bridge->terrain_animation_owned_updates),
                    static_cast<unsigned long long>(bridge->terrain_animation_fallback_updates),
                    static_cast<unsigned long long>(bridge->terrain_wave_sectors),
                    static_cast<unsigned long long>(bridge->terrain_animated_tile_updates));
        ImGui::Text(ui_text("Placed figures: %.3f ms / %llu calls"), bridge->figure_placed_cpu_ms,
                    static_cast<unsigned long long>(bridge->figure_placed_calls));
        ImGui::Text(ui_text("Lit figures: %.3f ms / %llu calls"), bridge->figure_lit_cpu_ms,
                    static_cast<unsigned long long>(bridge->figure_lit_calls));
        ImGui::Text(ui_text("Lit ingress morphed/rigid-or-unknown: %.3f/%0.3f ms  %llu/%llu calls"),
                    bridge->figure_lit_morphed_cpu_ms,
                    bridge->figure_lit_rigid_cpu_ms,
                    static_cast<unsigned long long>(bridge->figure_lit_morphed_calls),
                    static_cast<unsigned long long>(bridge->figure_lit_rigid_calls));
        ImGui::Text(ui_text("GPU-owned figures: %llu  CPU fallbacks: %llu"),
                    static_cast<unsigned long long>(bridge->figure_gpu_owned_calls),
                    static_cast<unsigned long long>(bridge->figure_cpu_fallback_calls));
        ImGui::Text(ui_text("GPU submit failures: %llu  High-level culled: %llu"),
                    static_cast<unsigned long long>(bridge->figure_gpu_submit_failures),
                    static_cast<unsigned long long>(bridge->figure_gpu_culled_calls));
        for (std::size_t index = 1; index < bridge->figure_fallback_reasons.size(); ++index) {
            const auto fallback_count = bridge->figure_fallback_reasons[index];
            if (!fallback_count) continue;
            ImGui::Text(ui_text("Fallback %-14s %llu"),
                        figure_fallback_reason_name(static_cast<FigureFallbackReason>(index)),
                        static_cast<unsigned long long>(fallback_count));
        }
    }
}

bool interface_option(const RendererOption& option) {
    if(option.group=="Interface" || option.group=="HUD layout" || option.group=="Combat UI")return true;
    std::size_t count=0;const auto* settings=player_graphics_settings(count);
    for(std::size_t i=0;i<count;++i)
        if(option.key==settings[i].key && std::strcmp(settings[i].category,"Interface")==0)return true;
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
// Reset UI asks first: a single click was too easy to hit by mistake.
void reset_ui_button(RendererConfig& config) {
    if(ImGui::Button(ui_text("Reset UI")))ImGui::OpenPopup("reset_ui_confirm");
    if(!ImGui::BeginPopupModal("reset_ui_confirm",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar))return;
    ImGui::TextUnformatted(ui_text("Reset the whole interface to its defaults?"));
    ImGui::TextDisabled("%s",ui_text("This changes the current interface profile."));
    if(ImGui::Button(ui_text("Reset UI"))){reset_interface(config);ImGui::CloseCurrentPopup();}
    ImGui::SameLine();
    if(ImGui::Button(ui_text("Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
// Options grouped "Diagnostics" (debug views such as the critter showcase)
// get their own tab; the Graphics tab lists every other group.
constexpr const char* kDiagnosticsGroup = "Diagnostics";
void draw_graphics_tab(RendererConfig& config, ImGuiTextFilter& filter, bool diagnostics = false,bool ui_only = false) {
    filter.Draw(ui_text("Search options"), -1.f);
    if (config.options().empty()) {
        ImGui::TextWrapped(ui_text("No runtime graphics options are registered by this renderer build. "
                           "Controls appear here only after the backing renderer feature exists."));
        return;
    }
    // The options that pass the tab and the search, and their groups in the
    // order each group first appears in the registration.
    std::vector<const RendererOption*> shown;
    std::vector<const std::string*> groups;
    for (const auto& option : config.options()) {
        if (option.group == "Combat UI" || option.type == RendererOptionType::text) continue;
        if(option.group=="HUD layout" || (option.group==kDiagnosticsGroup)!=diagnostics ||
           (!diagnostics && interface_option(option)!=ui_only))continue;
        const std::string searchable = std::string(ui_option_label(option)) + " " + option.label + " " + option.key +
                                       " " + option.group + " " + ui_option_description(option);
        if (!filter.PassFilter(searchable.c_str())) continue;
        shown.push_back(&option);
        if (std::none_of(groups.begin(), groups.end(), [&](const std::string* group) { return *group == option.group; }))
            groups.push_back(&option.group);
    }
    // One option row: the label wraps on the left, the control fills the
    // right, so long (translated) labels never clip.
    const auto draw_row = [&](const RendererOption& option) {
        const char* label = ui_option_label(option);
        const char* description = ui_option_description(option);
        ImGui::PushID(option.key.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        // Wrap short of the "(?)" marker so it stays inside the column.
        const float marker = *description ? ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x : 0.f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(1.f, ImGui::GetContentRegionAvail().x - marker));
        ImGui::TextUnformatted(label);
        ImGui::PopTextWrapPos();
        help_marker(description);
        if (option.restart_required) ImGui::TextDisabled("%s", ui_text("restart required"));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        switch (option.type) {
        case RendererOptionType::text: break;
        case RendererOptionType::boolean: {
            bool value = option.boolean_value;
            const char* note = blocked_option_note(config, option.key.c_str());
            ImGui::BeginDisabled(note != nullptr);
            if (ImGui::Checkbox("##value", &value)) config.set_boolean(option.key.c_str(), value);
            ImGui::EndDisabled();
            if (note) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", ui_text(note)); }
            break;
        }
        case RendererOptionType::integer: {
            int value = option.integer_value;
            if (option.key == "camp_ui_layout") {
                const char* choices[] = {ui_text("Original UI (comparison)"), ui_text("Wide grid")};
                if (ImGui::Combo("##value", &value, choices, 2))
                    config.set_integer(option.key.c_str(), value);
            } else if(option.key=="portrait_direction") {
                const char* choices[]={ui_text("Centered (original)"),ui_text("Top to bottom"),ui_text("Bottom to top"),ui_text("Left to right"),ui_text("Right to left")};
                if(ImGui::Combo("##value",&value,choices,5))config.set_integer(option.key.c_str(),value);
            } else if(option.key=="spell_bar_direction" || option.key=="ability_bar_direction" || option.key=="weapon_switcher_direction" || option.key=="quick_items_direction") {
                const char* choices[]={ui_text("Bottom to top"),ui_text("Top to bottom"),ui_text("Left to right"),ui_text("Right to left")};
                if(ImGui::Combo("##value",&value,choices,4))config.set_integer(option.key.c_str(),value);
            } else if (option.key == "anisotropy") {
                const char* choices[] = {"1", "2", "4", "8", "16"};
                if (ImGui::Combo("##value", &value, choices, 5))
                    config.set_integer(option.key.c_str(), value);
            } else if (option.key == "shadow_cascade_quality") {
                const char* choices[] = {ui_text("Low (1024 x 1024)"), ui_text("Medium (2048 x 2048)"),
                                         ui_text("High (4096 x 4096)")};
                if (ImGui::Combo("##value", &value, choices, 3))
                    config.set_integer(option.key.c_str(), value);
            } else if (option.key == "point_shadow_quality") {
                const char* choices[] = {ui_text("Low (128 x 128)"), ui_text("Medium (256 x 256)"),
                                         ui_text("High (512 x 512)")};
                if (ImGui::Combo("##value", &value, choices, 3))
                    config.set_integer(option.key.c_str(), value);
            } else if (option.key == "ui_language") {
                if (ImGui::Combo("##value", &value, ui_choices(kUiLanguageChoices)))
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
        ImGui::PopID();
    };
    // One header and one two-column table per group, its options in
    // registration order.
    for (const std::string* group : groups) {
        ImGui::SeparatorText(ui_text(group->c_str()));
        if (!ImGui::BeginTable(group->c_str(), 2, ImGuiTableFlags_SizingStretchSame)) continue;
        for (const RendererOption* option : shown)
            if (option->group == *group) draw_row(*option);
        ImGui::EndTable();
    }
}

void close_settings(DebugOverlay::Implementation& state,RendererConfig& config,bool keep);

void draw_hud_layout_control(DebugOverlay::Implementation& state,RendererConfig& config) {
    HudPanelBounds b;
    const bool available=game_hud_editor_available() && widescreen_hud_panel_bounds(HudPanel::messages,b);
    ImGui::BeginDisabled(!available && !game_hud_editing());
    if(ImGui::Button(ui_text(game_hud_editing() ? "Lock HUD layout" : "Unlock HUD layout"))) {
        if(game_hud_editing()) {set_game_hud_editing(false);state.pending_save=true;}
        else {
            if(state.settings_open)close_settings(state,config,true);
            config.set_overlay_mode(DebugOverlayMode::hidden);
            set_game_hud_editing(true);
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(ui_text("Drag anywhere inside each unlocked panel. Drag its corner to resize. F10 opens this menu again to lock and save."));
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
// Interface profiles (#187): eid3d11_profiles\<name>.ini; the current one
// (interface_profile, "default" unless chosen) receives every saved change.
bool valid_profile_name(const std::string& name) {
    if (name.empty() || name.size() > 48 || name.front() == '.' || name.front() == ' ' || name.back() == ' ') return false;
    for (const unsigned char c : name) if (c < 32 || std::strchr("\\/:*?\"<>|", c)) return false;
    return true;
}
std::string current_profile_name(const RendererConfig& config) {
    const auto* option = config.find("interface_profile");
    return option && valid_profile_name(option->text_value) ? option->text_value : std::string("default");
}
std::wstring profile_path(const std::string& name) {
    std::wstring wide(name.size(), L'\0');
    const int length = MultiByteToWideChar(CP_UTF8, 0, name.data(), int(name.size()), wide.data(), int(wide.size()));
    wide.resize(length > 0 ? std::size_t(length) : 0);
    return interface_profiles_directory() + L"\\" + wide + L".ini";
}
bool save_profile_file(const RendererConfig& config, const std::string& name) {
    CreateDirectoryW(interface_profiles_directory().c_str(), nullptr);
    return config.save_interface_profile(profile_path(name).c_str());
}
std::vector<std::string> list_profiles() {
    std::vector<std::string> names{"default"};
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((interface_profiles_directory() + L"\\*.ini").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return names;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring stem = found.cFileName;
        stem.resize(stem.size() - 4);
        char utf8[256]{};
        if (WideCharToMultiByte(CP_UTF8, 0, stem.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr) <= 0) continue;
        if (valid_profile_name(utf8) && std::find(names.begin(), names.end(), utf8) == names.end()) names.emplace_back(utf8);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(names.begin() + 1, names.end());
    return names;
}
void hud_profile_controls(DebugOverlay::Implementation& state, RendererConfig& config) {
    ImGui::SeparatorText(ui_text("Interface profile"));
    const std::string current = current_profile_name(config);
    if (ImGui::BeginCombo(ui_text("Profile"), current.c_str())) {
        for (const auto& name : list_profiles()) {
            if (!ImGui::Selectable(name.c_str(), name == current) || name == current) continue;
            // Keep this profile's unsaved edits, then switch.
            save_profile_file(config, current);
            const auto path = profile_path(name);
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) save_profile_file(config, name);
            else config.load_interface_profile(path.c_str());
            config.set_text("interface_profile", name.c_str());
            state.pending_save = true;
        }
        ImGui::EndCombo();
    }
    ImGui::InputText(ui_text("New name"), state.profile_name, sizeof(state.profile_name));
    ImGui::SameLine();
    const bool valid = valid_profile_name(state.profile_name);
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button(ui_text("Save as"))) {
        config.set_text("interface_profile", state.profile_name);
        save_profile_file(config, state.profile_name);
        state.pending_save = true;
        state.profile_name[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", ui_text(interface_profiles_help()));
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
    ImGui::TextUnformatted(ui_text(kHudPanels[panel].label));
    ImGui::Separator();
    ImGui::Text(ui_text("Position: X %.0f, Y %.0f px"),bounds.left,bounds.top);
    ImGui::Text(ui_text("Size: %.0f x %.0f px"),bounds.width,bounds.height);
    if(HudPanel(panel)==HudPanel::messages) {
        hud_scale_control(config,"message_panel_text_scale",ui_text("Text scale"));
    } else {
        const auto key=std::string(kHudPanels[panel].key)+"_scale";
        hud_scale_control(config,key.c_str(),ui_text("Widget scale"));
    }
    const std::string fade_prefix=kHudPanels[panel].key;
    if(const auto* fade=config.find((fade_prefix+"_auto_fade").c_str())){
        bool enabled=fade->boolean_value;
        if(ImGui::Checkbox(ui_text("Auto fade"),&enabled))config.set_boolean(fade->key.c_str(),enabled);
        hud_scale_control(config,(fade_prefix+"_fade_transparency").c_str(),ui_text("Faded transparency"));
        ImGui::TextDisabled(ui_text("0%% = solid; 100%% = hidden"));
    }
    if(panel>=unsigned(HudPanel::stance))ImGui::TextDisabled(ui_text("Fixed screen corner"));
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
            const char* bars[]={ui_text("Bottom to top"),ui_text("Top to bottom"),ui_text("Left to right"),ui_text("Right to left")};
            const char* portraits[]={ui_text("Centered (original)"),ui_text("Top to bottom"),ui_text("Bottom to top"),ui_text("Left to right"),ui_text("Right to left")};
            const bool faces=HudPanel(panel)==HudPanel::portraits;
            if(ImGui::Combo(ui_text("Icon alignment"),&value,faces?portraits:bars,faces?5:4))
                config.set_integer(direction,value);
        }
    }
    if(direction) {
        const std::string prefix=kHudPanels[panel].key;
        const auto* padding=config.find((prefix+"_padding").c_str());
        if(padding) {int value=padding->integer_value;if(ImGui::SliderInt(ui_text("Padding (px)"),&value,padding->integer_minimum,padding->integer_maximum))config.set_integer(padding->key.c_str(),value);}
        const auto* preview=config.find((prefix+"_preview").c_str());
        if(preview) {bool value=preview->boolean_value;if(ImGui::Checkbox(ui_text("Preview slots"),&value))config.set_boolean(preview->key.c_str(),value);}
        const auto* count=config.find((prefix+"_preview_count").c_str());
        if(count) {int value=count->integer_value;if(ImGui::SliderInt(ui_text("Preview count"),&value,count->integer_minimum,count->integer_maximum))config.set_integer(count->key.c_str(),value);}
        const auto* wrap=config.find((prefix+"_slots_per_column").c_str());
        if(wrap) {int value=wrap->integer_value;
            const bool rows=(direction && config.find(direction) && config.find(direction)->integer_value>=2);
            if(ImGui::SliderInt(ui_text(rows?"Slots per row":"Slots per column"),&value,wrap->integer_minimum,wrap->integer_maximum,value?"%d":ui_text("0 (one line)")))config.set_integer(wrap->key.c_str(),value);}
    }
    if(HudPanel(panel)==HudPanel::minimap || HudPanel(panel)==HudPanel::character_info || HudPanel(panel)==HudPanel::messages) {
        const std::string prefix=kHudPanels[panel].key;
        hud_scale_control(config,(prefix+"_buttons_scale").c_str(),ui_text("Buttons scale"));
        const auto* gap=config.find((prefix+"_border_padding").c_str());
        if(gap){int value=gap->integer_value;if(ImGui::SliderInt(ui_text("Border padding (px)"),&value,gap->integer_minimum,gap->integer_maximum))config.set_integer(gap->key.c_str(),value);}
        const auto* option=config.find((prefix+"_buttons_side").c_str());
        if(option) {int value=option->integer_value;
            const char* sides[]={ui_text("Automatic"),ui_text("Game default side"),ui_text("Left"),ui_text("Right"),ui_text("Top"),ui_text("Bottom")};
            if(ImGui::Combo(ui_text("Button alignment"),&value,sides,6))config.set_integer(option->key.c_str(),value);
        }
    }
    if(ImGui::Button(ui_text("Reset this panel")))reset_hud_panel(config,panel);
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
        if(ImGui::Begin(kHudPanels[i].label,nullptr,flags)) {
            ImGui::SetCursorScreenPos(at);ImGui::InvisibleButton("move_or_resize",hit_size);
            const bool active=ImGui::IsItemActive();
            const bool highlighted=ImGui::IsItemHovered() || active || ImGui::IsPopupOpen("Panel options");
            auto* draw=ImGui::GetWindowDrawList();
            const auto color=highlighted?IM_COL32(100,230,255,255):IM_COL32(255,215,80,255);
            if(highlighted)draw->AddRectFilled(at,ImVec2(at.x+hit_size.x,at.y+hit_size.y),IM_COL32(70,190,230,35));
            std::vector<HudPreviewSlot> previews;widescreen_hud_preview_slots(HudPanel(i),previews);
            for(const auto& preview:previews) {
                auto p=preview.bounds;hud_bounds_to_output(p);const ImVec2 first(p.left,p.top),last(p.left+p.width,p.top+p.height);
                draw->AddRectFilled(first,last,IM_COL32(150,185,210,22));
                draw->AddRect(first,last,IM_COL32(160,200,225,90),0.f,0,1.f);
                const std::string label=std::string(ui_text(i==4?"Party ":i==0?"Spell ":i==1?"Skill ":i==5?"Weapon ":"Item "))+std::to_string(preview.index+1);
                draw->AddText(ImVec2(first.x+3.f,first.y+3.f),IM_COL32(180,210,230,135),label.c_str());
            }
            // Editor guides are always one physical pixel, independent of widget scale.
            draw->AddRect(content,ImVec2(content.x+size.x,content.y+size.y),color,0.f,0,1.f);
            draw->AddText(ImVec2(at.x+5.f,at.y+5.f),color,ui_text(kHudPanels[i].label));
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
            if(ImGui::BeginPopupContextItem("Panel options")) {
                hud_panel_options(config,i,b);
                ImGui::EndPopup();
            }
        }
        ImGui::End();ImGui::PopStyleVar();
    }
    ImGui::SetNextWindowPos(ImVec2(20.f*state.ui_scale,layout.height*.4f),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290.f*state.ui_scale,0),ImGuiCond_FirstUseEver);
    if(ImGui::Begin(window_title("HUD layout","hud_layout").c_str(),nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_AlwaysAutoResize)) {
        hud_scale_control(config,"ui_scale",ui_text("Global UI scale"));
        const char* timing_keys[]={"hud_fade_in_duration","hud_fade_duration","hud_fade_delay"};
        const char* timing_labels[]={ui_text("Fade-in duration"),ui_text("Fade-out duration"),ui_text("Idle time before fade-out")};
        for(unsigned i=0;i<3;++i)if(const auto* timing=config.find(timing_keys[i])) {
            int value=timing->integer_value;
            if(ImGui::SliderInt(timing_labels[i],&value,timing->integer_minimum,timing->integer_maximum,ui_text("%d ms")))
                config.set_integer(timing->key.c_str(),value);
        }
        ImGui::Checkbox(ui_text("Show grid"),&state.hud_grid);
        hud_profile_controls(state,config);
        ImGui::Separator();
        reset_ui_button(config);
        ImGui::SameLine();
        if(ImGui::Button(ui_text("Lock"))){set_game_hud_editing(false);state.pending_save=true;}
        ImGui::TextWrapped(ui_text("Drag anywhere in a panel; resize its lower-right corner. Right-click for this panel's options, coordinates and reset."));
        ImGui::TextWrapped(ui_text("Ctrl snaps horizontally, Alt vertically; hold both for both axes. Snaps use the visible grid, otherwise screen and widget edges. Lock saves; Esc also locks."));
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
    if (ImGui::Begin(window_title("EID3D11 renderer", "renderer").c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("%s", ui_text(state.developer
            ? "F10: hidden / monitor / configuration   Shift+F10: player settings   F11: capture next frame"
            : "Shift+F10: hidden / monitor / configuration   F10: player settings   F11: capture next frame"));
        if (ImGui::BeginTabBar("RendererTabs")) {
            if (ImGui::BeginTabItem(window_title("Performance", "performance").c_str())) {
                draw_performance_tab(state, profile, bridge, resources);
                ImGui::Separator();
                if (ImGui::Button(ui_text("Capture next frame, pass by pass"))) state.capture_requested = true;
                if (ImGui::Button(ui_text("Freeze / unfreeze culling camera (Shift+F11)"))) state.cull_freeze_toggle = true;
                help_marker(ui_text("Explicit diagnostic capture. It may stall that captured frame; normal monitoring never writes to disk."));
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(window_title("Graphics", "graphics").c_str())) {
                if (!state.settings_open && ImGui::Button(ui_text("Open player Graphics Settings")))
                    open_settings(state, overlay.config(), false);
                draw_graphics_tab(overlay.config(), state.graphics_filter);
                ImGui::Separator();
                if (ImGui::Button(ui_text("Apply and save"))) state.pending_save = true;
                ImGui::SameLine();
                ImGui::TextDisabled(ui_text("Local eid3d11.ini; no registry settings"));
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(window_title("Interface", "interface").c_str())) {
                draw_hud_layout_control(state,overlay.config());
                ImGui::SameLine();reset_ui_button(overlay.config());
                hud_profile_controls(state,overlay.config());
                draw_graphics_tab(overlay.config(),state.graphics_filter,false,true);
                if(ImGui::CollapsingHeader(ui_text("Combat healthplates and effects")))state.combat_ui.draw_settings(overlay.config());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(window_title(kDiagnosticsGroup, "diagnostics").c_str())) {
                draw_graphics_tab(overlay.config(), state.diagnostics_filter, true);
                ImGui::Separator();
                if (ImGui::Button(ui_text("Spawn lightning at camera focus"))) owned::request_debug_lightning();
                if (ImGui::Button(ui_text("Apply and save##diagnostics"))) state.pending_save = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(window_title("Resources", "resources").c_str())) {
                const DebugResourceView selected = overlay.config().debug_resource_view();
                if (ImGui::BeginCombo(ui_text("Resource view"),
                                      ui_text(debug_resource_view_label(selected)))) {
                    for (std::uint8_t raw = 0;
                         raw < static_cast<std::uint8_t>(DebugResourceView::count); ++raw) {
                        const auto candidate = static_cast<DebugResourceView>(raw);
                        const bool available = debug_views.available(candidate);
                        ImGui::BeginDisabled(!available);
                        if (ImGui::Selectable(ui_text(debug_resource_view_label(candidate)),
                                              candidate == selected) && available)
                            overlay.config().set_debug_resource_view(candidate);
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                if (!debug_views.available(selected))
                    ImGui::TextDisabled(ui_text("Selected view is unavailable in the current frame."));
                ImGui::TextWrapped(ui_text("Views use only resources already owned by the renderer. "
                                   "Unavailable production buffers are listed explicitly and are not fabricated."));
                ImGui::TextDisabled(ui_text("The selected view runs only while this full F10 window is active."));
                if (selected == DebugResourceView::shadows ||
                    selected == DebugResourceView::shadow_cascade_coverage) {
                    ImGui::Separator();
                    ImGui::Text(ui_text("Runtime shadows: %s"), ui_text(shadows.feature_enabled ? "ON" : "OFF"));
                    ImGui::Text(ui_text("Shadow masks (bits C1..C4): enabled 0x%X, valid 0x%X"),
                                shadows.enabled_mask, shadows.valid_mask);
                    if (shadows.pass_in_progress)
                        ImGui::TextDisabled(ui_text("Shadow pass in progress; maps are not displayed."));
                    else
                        ImGui::Text(ui_text("Rewritten in last completed shadow pass: 0x%X"),
                                    shadows.rewritten_mask);
                    constexpr ImVec4 colors[] = {{1,.25f,.2f,1}, {.2f,1,.25f,1},
                                                 {.2f,.5f,1,1}, {1,.85f,.2f,1}, {.8f,.35f,1,1}};
                    ImGui::TextWrapped(ui_text("Maps: C1-C3 top row; C4 middle distance and C5 far distance below. "
                        "Gray is each map's normalized light depth [0,1]; navy is clear depth. "
                        "Colored rectangles show stored receiver XY bounds. Hatched maps are empty or invalid."));
                    ImGui::TextWrapped(ui_text("Coverage colors receiving world pixels: C1 red, C2 green, C3 blue, C4 yellow, C5 violet. "
                        "Gray: no selector; magenta: first selector invalid; cyan: outside selected light volume. "
                        "Unlit visuals and UI keep their colors. Changes apply to the next world render."));
                    for (unsigned i = 0; i < 5; ++i) {
                        const auto& slice = shadows.slices[i];
                        const bool enabled = (shadows.enabled_mask & (1u << i)) != 0;
                        const bool valid = (shadows.valid_mask & (1u << i)) != 0;
                        ImGui::TextColored(colors[i], "C%u  %s", i + 1,
                                           ui_text(!enabled ? "empty / disabled" : (valid ? "valid" : "invalid")));
                        if (!enabled) continue;
                        ImGui::Text(ui_text("Radius %.4g  |  world units / texel %.6g  |  caster revision entries %llu"),
                            slice.radius, slice.world_units_per_texel,
                            static_cast<unsigned long long>(slice.caster_revision_entries));
                        ImGui::Text(ui_text("Receiver light XY: [%.4g, %.4g] to [%.4g, %.4g]"),
                            slice.receiver_minimum[0], slice.receiver_minimum[1],
                            slice.receiver_maximum[0], slice.receiver_maximum[1]);
                        ImGui::Text(ui_text("Receiver light Z: [%.4g, %.4g]  |  map light Z: [%.4g, %.4g]"),
                            slice.receiver_minimum[2], slice.receiver_maximum[2],
                            slice.light_depth[0], slice.light_depth[1]);
                    }
                }
                if (ImGui::Button(ui_text("Save resource view selection"))) state.pending_save = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(window_title("Attribution markers", "markers").c_str())) {
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
                    "inactive", "begin armed", "active", "end armed", "complete", "overflow", "invalid" };
                const auto state_index = static_cast<std::size_t>(status.state);
                ImGui::Text(ui_text("Recorder: %s%s  segments: %u / %u"),
                            ui_text(state_index < std::size(states) ? states[state_index] : "invalid"),
                            status.window_flush_pending ? ui_text(" (draining/flushing window)") : "",
                            status.completed, status.capacity);
                ImGui::Text(ui_text("Current marked window: %u / %u frames"),
                            status.window_frame_count, status.window_frame_capacity);
                ImGui::TextDisabled(ui_text("At the measured ~600 FPS, 8192 frames are ~13.65 s per marked window."));
                if (!available)
                    ImGui::TextDisabled(ui_text("Enable EI_RENDERER_ATTRIBUTION=1 before launch to arm markers."));
                ImGui::Combo(ui_text("Stable route label"), &state.marker_label, labels,
                             static_cast<int>(std::size(labels)));
                ImGui::Checkbox(ui_text("Issue #13 menu opening transition"),
                                &state.marker_issue13_transition);
                AttributionMarkerSelection selection{};
                selection.label = static_cast<AttributionMarkerLabel>(state.marker_label);
                selection.issue13_menu_transition = state.marker_issue13_transition;
                if (state.marker_issue13_transition) {
                    const auto menu_label_getter = [](void*, int index) -> const char* {
                        return attribution_issue13_menu_label_name(
                            static_cast<AttributionIssue13MenuLabel>(index));
                    };
                    ImGui::Combo(ui_text("Stable non-semantic menu label"), &state.marker_menu_label,
                                 menu_label_getter, nullptr,
                                 static_cast<int>(AttributionIssue13MenuLabel::count));
                    ImGui::Text(ui_text("Opening ordinal: %d"), state.marker_opening_ordinal);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(state.marker_opening_ordinal <= 1);
                    if (ImGui::Button("-##opening_ordinal")) --state.marker_opening_ordinal;
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(
                        state.marker_opening_ordinal == (std::numeric_limits<int>::max)());
                    if (ImGui::Button("+##opening_ordinal")) ++state.marker_opening_ordinal;
                    ImGui::EndDisabled();
                    ImGui::Combo(ui_text("Opening observation"), &state.marker_repetition,
                                 "first_observed\0repeated_opening\0");
                    ImGui::TextDisabled(ui_text("Ordinal is not interpreted as cold or warm resource state."));
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
                        ImGui::TextDisabled(ui_text("Begin unavailable: select complete valid marker metadata."));
                    else if (status.window_flush_pending)
                        ImGui::TextDisabled(ui_text("Begin unavailable: wait for the completed window to flush."));
                    else
                        ImGui::TextDisabled(ui_text("Begin unavailable: finish or restart the current recorder state."));
                }
                ImGui::BeginDisabled(!can_begin);
                if (ImGui::Button(ui_text("Begin at next successful Present"))) {
                    profiler->arm_attribution_marker_begin(selection);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                const bool can_end = available && status.state == AttributionMarkerState::active;
                ImGui::BeginDisabled(!can_end);
                if (ImGui::Button(ui_text("End at next successful Present")))
                    profiler->arm_attribution_marker_end();
                ImGui::EndDisabled();
                ImGui::TextWrapped(ui_text("Markers are explicit half-open Present intervals. Completed windows flush as bounded batches only after delayed GPU queries drain in the unmarked gap. No scene detection, gameplay hook, or global marker hotkey is active."));
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
        // A profile switch inside the window already saved; save the revert.
        state.pending_save = true;
    }
    state.settings_open = false;
    state.settings_first_run = false;
    state.settings_snapshot.clear();
}

// Options that size one group of the video memory breakdown; the window
// shows what that group holds now beside the option.
bool setting_memory_group(const char* key, VideoMemoryGroup& group) noexcept {
    static constexpr struct { const char* key; VideoMemoryGroup group; } kRows[] = {
        {"render_scale", VideoMemoryGroup::screen},
        {"upscaler", VideoMemoryGroup::screen},
        {"water_reflection_quality", VideoMemoryGroup::water},
        {"shadow_cascade_quality", VideoMemoryGroup::sun_shadows},
        {"point_shadow_quality", VideoMemoryGroup::lamp_shadows},
        {"fog_volume_quality", VideoMemoryGroup::fog},
        {"ao_quality", VideoMemoryGroup::ambient_occlusion},
        {"sky_clouds", VideoMemoryGroup::sky},
        {"terrain_blend", VideoMemoryGroup::terrain_blend},
        {"ground_cover", VideoMemoryGroup::ground_cover},
        {"critters", VideoMemoryGroup::critters},
    };
    for (const auto& row : kRows)
        if (std::strcmp(row.key, key) == 0) { group = row.group; return true; }
    return false;
}

void text_megabytes(std::uint64_t bytes) {
    const double value = mib(bytes);
    ImGui::TextDisabled(ui_text(value < 10.0 ? "%.1f MB" : "%.0f MB"), value);
}

// The process memory line beside the video memory: the 32-bit game's
// address space in use and its largest free block (what large maps run out
// of), and the 64-bit render host's private memory when one runs. Sampled at
// most once a second while drawn. Core mode draws this on the host: the game
// process sends its address space through the control block
// (debug_overlay_set_game_address_space); the host reads its own process.
void draw_process_memory() {
    struct Sample {
        ULONGLONG at_ms = 0;
        std::uint64_t game_used_mb = 0, game_free_mb = 0, host_private_bytes = 0;
    };
    static Sample sample;
    const ULONGLONG now = GetTickCount64();
    if (!sample.at_ms || now - sample.at_ms >= 1000) {
        sample = {};
        sample.at_ms = now;
        if (g_running_core) {
            sample.game_used_mb = g_core_game_va_used_mb.load(std::memory_order_relaxed);
            sample.game_free_mb = g_core_game_va_free_mb.load(std::memory_order_relaxed);
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                     sizeof(counters)))
                sample.host_private_bytes = counters.PrivateUsage;
        } else {
            address_space_mb(sample.game_used_mb, sample.game_free_mb, true);
            sample.host_private_bytes = x64remote::host_private_bytes();
        }
    }
    if (sample.game_used_mb)
        ImGui::Text(ui_text("Game address space: %.0f MB used, largest free block %.0f MB"),
                    static_cast<double>(sample.game_used_mb), static_cast<double>(sample.game_free_mb));
    else
        ImGui::TextUnformatted(ui_text("Game address space: n/a"));
    ImGui::SetItemTooltip(ui_text("The 32-bit game can address at most 4 GB (2 GB without 4 GB mode). "
                          "When the largest free block gets small, large maps can run out of memory."));
    if (sample.host_private_bytes) {
        ImGui::Text(ui_text("Render host memory: %.0f MB"), mib(sample.host_private_bytes));
        ImGui::SetItemTooltip(ui_text("Private memory of the 64-bit render host process, which holds the graphics driver "
                              "and the renderer's own data outside the game's address space."));
    }
}

// "Video memory: X MB used of Y MB available" and the
// per-feature list (VideoMemoryBreakdown) of the settings window.
void draw_video_memory(const DebugOverlay::Implementation& state, const RendererResourceSnapshot& resources) {
    const ImVec4 warning(1.f, .8f, .3f, 1.f);
    const VideoMemoryBreakdown& memory = resources.video_memory;
    const bool measured = !resources.system_telemetry_stale && resources.gpu_memory_valid;
    const std::uint64_t budget = measured ? resources.gpu_budget_bytes : state.adapter_budget_bytes;
    const std::uint64_t own = memory.valid ? memory.total() : 0;
    if (measured)
        ImGui::Text(ui_text("Video memory: %.0f MB used of %.0f MB available"), mib(resources.gpu_usage_bytes), mib(budget));
    else if (own && budget)
        ImGui::Text(ui_text("Video memory: about %.0f MB used of %.0f MB available"), mib(own), mib(budget));
    else
        ImGui::TextUnformatted(ui_text("Video memory: n/a"));
    ImGui::SetItemTooltip(ui_text("Graphics card memory the game uses now, and the share Windows makes available to it. "
                          "Settings changes show here within a few seconds."));
    const std::uint64_t used = measured ? std::max(resources.gpu_usage_bytes, own) : own;
    if (budget && used > budget) {
        ImGui::SameLine();
        ImGui::TextColored(warning, ui_text("More than available"));
        ImGui::SetItemTooltip(ui_text("The game needs more video memory than this card offers, which causes stutter. "
                              "Lower the render scale, shadow resolution, fog quality or ground cover."));
    }
    draw_process_memory();
    if (resources.vram_budget.enforced && resources.vram_budget.level) {
        ImGui::TextColored(warning, ui_text("Reduced to fit video memory: %s (about %.0f MB needed)"),
                           ui_text(vram_budget_level_text(resources.vram_budget.level)),
                           mib(resources.vram_budget.estimate_bytes()));
        ImGui::SetItemTooltip(ui_text("These settings need more video memory than the card has, so textures and shadows are "
                              "drawn at lower resolution. Lower settings bring them back."));
    }
    if (!memory.valid || !ImGui::CollapsingHeader(ui_text("Video memory by feature"))) return;
    std::array<std::size_t, kVideoMemoryGroupCount> order{};
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return memory.bytes[a] > memory.bytes[b]; });
    if (!ImGui::BeginTable("video_memory", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) return;
    const auto row = [](const char* label) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
    };
    for (const std::size_t index : order) {
        if (!memory.bytes[index]) continue;
        row(ui_text(video_memory_group_name(static_cast<VideoMemoryGroup>(index))));
        text_megabytes(memory.bytes[index]);
    }
    row(ui_text("Terrain material cache"));
    if (memory.terrain_cache_known) text_megabytes(memory.terrain_cache_bytes);
    else ImGui::TextDisabled("%s", ui_text("n/a"));
    if (measured && resources.gpu_usage_bytes > own) {
        row(ui_text("Other (driver, shaders, not listed)"));
        text_megabytes(resources.gpu_usage_bytes - own);
    }
    ImGui::EndTable();
}

// The Upscaling method: Off, DLSS when the x64 host can run it, FSR 2 when
// this device can; why one is missing is shown below the list. A saved
// method that is unavailable is marked here; embedded DLSS refuses world frames.
void draw_upscaler_choice(RendererConfig& config, const RendererOption& option) {
    static constexpr const char* kLabels[kUpscalerMethodMaximum + 1] = {"Off", "NVIDIA DLSS", "AMD FSR 2"};
    const char* dlss_reason = dlss_unavailable_reason();
    const char* fsr_reason = fsr_unavailable_reason();
    const auto available = [&](int value) {
        return value == kUpscalerOff || (value == kUpscalerDlss ? !dlss_reason : !fsr_reason);
    };
    const int current = std::clamp(option.integer_value, 0, kUpscalerMethodMaximum);
    char preview[160]{};
    std::snprintf(preview, sizeof(preview), available(current) ? "%s" : "%s (%s)", ui_text(kLabels[current]),
                  ui_text("unavailable"));
    const char* note = blocked_option_note(config, option.key.c_str());
    ImGui::BeginDisabled(note != nullptr);
    if (ImGui::BeginCombo("##value", preview)) {
        for (int value = 0; value <= kUpscalerMethodMaximum; ++value)
            if (available(value) && ImGui::Selectable(ui_text(kLabels[value]), value == current))
                config.set_integer(option.key.c_str(), value);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (dlss_reason) ImGui::TextDisabled("DLSS: %s", ui_text(dlss_reason));
    if (fsr_reason) ImGui::TextDisabled("FSR: %s", ui_text(fsr_reason));
}

// The quality presets of the running method, in that method's own names
// (DLAA for DLSS, Native AA for FSR); greyed with a note while upscaling is off.
void draw_upscaler_quality_choice(RendererConfig& config, const RendererOption& option) {
    static constexpr const char* kLabels[2][kUpscalerQualityMaximum + 1] = {
        {"Quality", "Balanced", "Performance", "Ultra Performance", "DLAA"},
        {"Quality", "Balanced", "Performance", "Ultra Performance", "Native AA"}};
    const int method = active_upscaler(config);
    const auto& labels = kLabels[method == kUpscalerDlss ? 0 : 1];
    const int current = std::clamp(option.integer_value, 0, kUpscalerQualityMaximum);
    const char* note = blocked_option_note(config, option.key.c_str());
    ImGui::BeginDisabled(note != nullptr);
    if (ImGui::BeginCombo("##value", ui_text(labels[current]))) {
        for (int value = 0; value <= kUpscalerQualityMaximum; ++value)
            if (ImGui::Selectable(ui_text(labels[value]), value == current)) config.set_integer(option.key.c_str(), value);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (note) ImGui::TextDisabled("%s", ui_text(note));
}

void draw_player_setting(RendererConfig& config, const PlayerGraphicsSetting& setting, bool developer,
                         DebugOverlay::Implementation& state, const VideoMemoryBreakdown& memory) {
    const RendererOption* option = config.find(setting.key);
    if (!option) {
        // Visible to developers only: a table key the renderer never registered.
        if (developer) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(1.f, .35f, .2f, 1.f), ui_text("Missing option: %s"), setting.key);
        }
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    const char* tooltip = ui_setting_tooltip(setting);
    // Russian labels are longer and wrap; English keeps its single line.
    if (ui_russian()) ImGui::TextWrapped("%s", ui_setting_label(setting));
    else ImGui::TextUnformatted(setting.label);
    ImGui::SetItemTooltip("%s", tooltip);
    if (option->restart_required) { ImGui::SameLine(); ImGui::TextDisabled("%s", ui_text("restart required")); }
    if (VideoMemoryGroup group{}; memory.valid && setting_memory_group(setting.key, group)) {
        ImGui::SameLine();
        text_megabytes(memory.bytes[static_cast<std::size_t>(group)]);
        ImGui::SetItemTooltip(ui_text("Video memory this feature uses now."));
    }
    ImGui::TableNextColumn();
    ImGui::PushID(setting.key);
    ImGui::SetNextItemWidth(-FLT_MIN);
    switch (option->type) {
    case RendererOptionType::boolean: {
        bool value = option->boolean_value;
        const char* note = blocked_option_note(config, setting.key);
        ImGui::BeginDisabled(note != nullptr);
        if (ImGui::Checkbox("##value", &value)) config.set_boolean(setting.key, value);
        ImGui::EndDisabled();
        if (note) { ImGui::SameLine(); ImGui::TextDisabled("%s", ui_text(note)); }
        // The renderer this session actually runs (read-only).
        if (g_running_renderer && std::strcmp(setting.key, "x64_render_host") == 0)
            ImGui::TextDisabled(ui_text("Running: %s"), ui_text(g_running_renderer));
        // A switch with a "<key>_weight" percent option (critter types) shows
        // that weight beside it, greyed while the switch is off.
        const RendererOption* weight = config.find((std::string(setting.key) + "_weight").c_str());
        if (weight && weight->type == RendererOptionType::integer) {
            ImGui::SetItemTooltip("%s", tooltip);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int percent = weight->integer_value;
            ImGui::BeginDisabled(!value);
            if (ImGui::SliderInt("##weight", &percent, weight->integer_minimum, weight->integer_maximum,
                                 ui_text("%d%% spawn weight"), ImGuiSliderFlags_Logarithmic))
                config.set_integer(weight->key.c_str(), percent);
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", ui_text("How often this type is picked against the other types that fit the place. "
                                                "100% is the normal mix; 0% keeps it away. The number of critters stays the same."));
            ImGui::PopID();
            return;
        }
        break;
    }
    case RendererOptionType::integer: {
        if (std::strcmp(setting.key, "upscaler") == 0) {
            draw_upscaler_choice(config, *option);
        } else if (std::strcmp(setting.key, "upscaler_quality") == 0) {
            draw_upscaler_quality_choice(config, *option);
        } else if (std::strcmp(setting.key, "render_scale") == 0) {
            char preview[16]{};
            std::snprintf(preview, sizeof(preview), "%d%%", option->integer_value);
            const char* note = blocked_option_note(config, setting.key);
            ImGui::BeginDisabled(note != nullptr);
            if (ImGui::BeginCombo("##value", preview)) {
                for (const int percent : kRenderScaleChoices) {
                    char label[16]{};
                    std::snprintf(label, sizeof(label), "%d%%", percent);
                    if (ImGui::Selectable(label, percent == option->integer_value))
                        config.set_integer(setting.key, percent);
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (note) ImGui::TextDisabled("%s", ui_text(note));
        } else if (std::strcmp(setting.key, "window_width") == 0) {
            // One row for both client dimensions; 0 x 0 is the monitor's size.
            int width = option->integer_value, height = configured_value(config, "window_height");
            char preview[64]{};
            std::snprintf(preview, sizeof(preview), "%s", ui_text("Native"));
            if (width > 0 && height > 0) format_window_size(preview, sizeof(preview), width, height);
            if (ImGui::BeginCombo("##value", preview)) {
                if (ImGui::Selectable(ui_text("Native"), width <= 0 || height <= 0)) {
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
            ImGui::SetItemTooltip(ui_text("Client width in pixels. Set both dimensions to 0 for the monitor's native size."));
            ImGui::SameLine();
            ImGui::TextUnformatted("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(field_width);
            if (ImGui::InputInt("##height", &height, 0, 0)) config.set_integer("window_height", height);
            ImGui::SetItemTooltip(ui_text("Client height in pixels. A window larger than the work area is fitted to the screen."));
            ImGui::BeginDisabled(state.display_apply_pending);
            if (ImGui::Button(ui_text("Apply display"))) {
                state.display_apply = {configured_value(config, "display_mode") == 1 ? DisplayMode::windowed : DisplayMode::borderless,
                    configured_value(config, "window_width"), configured_value(config, "window_height")};
                state.display_apply_pending = true;
                state.display_apply_result[0] = 0;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(ui_text("Apply without restarting. Borderless uses the monitor size; windowed fits the requested client size to the work area."));
            if (state.display_apply_result[0]) ImGui::TextWrapped("%s", state.display_apply_result);
        } else if (std::strcmp(setting.key, "frame_limit") == 0) {
            // Off / fixed rates / Custom; Custom shows a rate slider below.
            const char* labels = ui_choices("Off\0" "30\0" "60\0" "90\0" "120\0" "144\0" "165\0" "240\0" "Custom\0");
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
            if (ImGui::Combo("##value", &index, ui_choices(setting.choices)))
                config.set_integer(setting.key, option->integer_minimum + index);
        } else {
            int value = option->integer_value;
            const bool fade_transparency=std::strstr(setting.key,"_fade_transparency")!=nullptr;
            const char* format=ui_text((std::strcmp(setting.key,"hud_fade_duration")==0 || std::strcmp(setting.key,"hud_fade_in_duration")==0 || std::strcmp(setting.key,"hud_fade_delay")==0)?"%d ms":
                fade_transparency?(value==0?"%d%% (solid)":value==100?"%d%% (hidden)":"%d%%"):"%d");
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
        const char* note = blocked_option_note(config, setting.key);
        ImGui::BeginDisabled(note != nullptr);
        if (ImGui::SliderFloat("##value", &value, option->floating_minimum, option->floating_maximum, "%.2f"))
            config.set_floating_point(setting.key, value);
        ImGui::EndDisabled();
        if (note) ImGui::TextDisabled("%s", ui_text(note));
        break;
    }
    }
    ImGui::SetItemTooltip("%s", tooltip);
    ImGui::PopID();
}

void draw_samgbur_lod(DebugOverlay::Implementation& state,const SamgburLodSnapshot& data) {
    if(!state.settings_filter.PassFilter("samgbur01 LOD"))return;
    if(!ImGui::CollapsingHeader("samgbur01 LOD",ImGuiTreeNodeFlags_DefaultOpen))return;
    if(!data.instances){ImGui::TextDisabled("%s",ui_text("No samgbur01 submitted this frame."));return;}
    state.samgbur_lod_instance=std::clamp(state.samgbur_lod_instance,0,static_cast<int>(data.instances)-1);
    int instance=state.samgbur_lod_instance+1;
    if(data.instances>1&&ImGui::SliderInt(ui_text("LOD instance"),&instance,1,static_cast<int>(data.instances)))
        state.samgbur_lod_instance=instance-1;
    if(data.state==SamgburLodSnapshot::unavailable){ImGui::TextDisabled("%s",ui_text("GPU LOD data unavailable."));return;}
    if(data.state!=SamgburLodSnapshot::ready||data.instance!=static_cast<unsigned>(state.samgbur_lod_instance+1)){
        ImGui::TextDisabled("%s",ui_text("Waiting for GPU LOD sample..."));return;
    }
    if(!data.visible)ImGui::TextUnformatted(ui_text("Instance is not visible in the color pass."));
    else if(data.mode&16u)ImGui::TextUnformatted(ui_text("Impostor (not a triangle LOD)."));
    else if(data.second!=UINT32_MAX)
        ImGui::Text(ui_text("Current level: L%u -> L%u (%u%%)"),data.first,data.second,(data.coverage*100u+127u)/255u);
    else ImGui::Text(ui_text("Current level: L%u"),data.first);
    if(ImGui::BeginTable("samgbur_lod_levels",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH)){
        ImGui::TableSetupColumn("LOD");ImGui::TableSetupColumn(ui_text("Triangles"));ImGui::TableHeadersRow();
        for(unsigned level=0;level<4;++level){
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("L%u",level);ImGui::TableNextColumn();
            if(level==0||level<=data.levels)ImGui::Text("%u",data.triangles[level]);
            else ImGui::TextDisabled("%s",ui_text("Not built"));
        }
        ImGui::EndTable();
    }
    if(!data.levels)ImGui::TextWrapped("%s",ui_text("No reduced LOD levels were built."));
    else if(data.visible&&!(data.mode&2))ImGui::TextUnformatted(ui_text("Object LOD is disabled."));
    else if(data.visible&&!(data.mode&1))ImGui::TextUnformatted(ui_text("This part is not eligible for LOD."));
    ImGui::TextDisabled(ui_text("GPU sample age: %llu frames"),static_cast<unsigned long long>(data.age_frames));
}

void draw_settings_window(DebugOverlay::Implementation& state, RendererConfig& config,
                          const RendererResourceSnapshot& resources) {
    const float scale = state.ui_scale;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f), ImGuiCond_Appearing,
                            ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(680.f * scale, io.DisplaySize.x * .95f),
                                    std::min(660.f * scale, io.DisplaySize.y * .9f)), ImGuiCond_Appearing);
    bool keep_open = true;
    bool accept = false, cancel = false, save_over_budget = false;
    if (ImGui::Begin(window_title("Options", "options").c_str(), state.settings_first_run ? nullptr : &keep_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        if (state.settings_first_frame) {
            ImGui::SetWindowFocus();
            state.settings_first_frame = false;
        }
        if (state.settings_first_run) {
            ImGui::TextWrapped(ui_text("Welcome. Choose the graphics quality for this PC. "
                               "You can change it at any time with F10."));
            if (state.suggested_preset != GraphicsPreset::custom)
                ImGui::Text(ui_text("Suggested for %s: %s"), state.adapter_name[0] ? state.adapter_name : ui_text("this graphics card"),
                            ui_text(graphics_preset_name(state.suggested_preset)));
        }

        if (!state.large_address_aware) {
            ImGui::TextColored(ImVec4(1.f, .8f, .3f, 1.f), ui_text("4 GB memory mode is off"));
            ImGui::SetItemTooltip(ui_text("game.exe can use only 2 GB of memory; large maps with high settings may run out."));
            if (state.large_address_result) {
                ImGui::TextWrapped("%s", state.large_address_result);
            } else if (ImGui::Button(ui_text("Enable 4 GB mode (restart required)"))) {
                state.large_address_result = enable_large_address_mode();
            }
        }
        if (state.developer)
            ImGui::TextDisabled(ui_text("Developer mode: VSync and the frame rate limit are forced off."));
        const GraphicsPreset detected = detect_graphics_preset(config);
        if (detected != GraphicsPreset::custom) state.preset_slider = static_cast<int>(detected);
        // Its own section: not part of the interface (Reset UI, profiles).
        ImGui::SeparatorText(ui_text("Language"));
        int language = configured_value(config, "ui_language");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##ui_language", &language, ui_choices(kUiLanguageChoices)))
            config.set_integer("ui_language", language);
        ImGui::SeparatorText(ui_text("Preset"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        int slider = state.preset_slider;
        if (ImGui::SliderInt("##preset", &slider, 0, kGraphicsPresetCount - 1, ui_text(graphics_preset_name(detected)),
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            state.preset_slider = slider;
            apply_graphics_preset(config, static_cast<GraphicsPreset>(slider));
        }
        ImGui::SetItemTooltip(ui_text("Drag to choose Low, Medium, High or Ultra. Changing an advanced option below makes it Custom."));
        // Stop labels under the slider.
        const float width = ImGui::GetContentRegionAvail().x;
        const float left = ImGui::GetCursorPosX();
        for (int index = 0; index < kGraphicsPresetCount; ++index) {
            const char* name = ui_text(graphics_preset_name(static_cast<GraphicsPreset>(index)));
            const float text = ImGui::CalcTextSize(name).x;
            const float at = left + (width - text) * static_cast<float>(index) / (kGraphicsPresetCount - 1);
            if (index) ImGui::SameLine();
            ImGui::SetCursorPosX(at);
            ImGui::TextDisabled("%s", name);
        }
        ImGui::TextWrapped("%s: %s", ui_text(graphics_preset_name(detected)), ui_text(graphics_preset_summary(detected)));
        if (!state.settings_first_run && state.suggested_preset != GraphicsPreset::custom)
            ImGui::TextDisabled(ui_text("Suggested for %s: %s"), state.adapter_name[0] ? state.adapter_name : ui_text("this graphics card"),
                                ui_text(graphics_preset_name(state.suggested_preset)));
        draw_video_memory(state, resources);

        if (configured_value(config, "display_mode") != state.running_display[0] ||
            configured_value(config, "window_width") != state.running_display[1] ||
            configured_value(config, "window_height") != state.running_display[2])
            ImGui::TextDisabled(ui_text("Display changes are pending. Use Apply display beside Window size."));
        state.settings_filter.Draw(ui_text("Search options"),-1.f);
        const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y;
        if(ImGui::BeginTabBar("option_tabs")) {
          for(unsigned tab=0;tab<2;++tab) {
            const bool ui_tab=tab==1;
            if(!ImGui::BeginTabItem(window_title(ui_tab?"Interface":"Graphics",ui_tab?"interface":"graphics").c_str()))continue;
            if(ui_tab) {
                draw_hud_layout_control(state,config);ImGui::SameLine();
                reset_ui_button(config);
                hud_profile_controls(state,config);
            }
            if(ImGui::BeginChild(ui_tab?"interface_options":"graphics_options",ImVec2(0,-footer),ImGuiChildFlags_Borders)) {
            if(!ui_tab)draw_samgbur_lod(state,resources.samgbur_lod);
            std::size_t category_count = 0, setting_count = 0;
            const char* const* categories = player_graphics_categories(category_count);
            const PlayerGraphicsSetting* settings = player_graphics_settings(setting_count);
            auto matches = [&](const PlayerGraphicsSetting& setting) {
                const std::string text = std::string(ui_setting_label(setting)) + " " + setting.label + " " + setting.key + " " +
                                         ui_text(setting.category) + " " + ui_setting_tooltip(setting);
                return state.settings_filter.PassFilter(text.c_str());
            };
            bool any_match = !ui_tab && state.settings_filter.PassFilter("samgbur01 LOD");
            for (std::size_t category = 0; category < category_count; ++category) {
                if((std::strcmp(categories[category],"Interface")==0)!=ui_tab)continue;
                bool category_match = false;
                for (std::size_t index = 0; index < setting_count; ++index)
                    if (std::strcmp(settings[index].category, categories[category]) == 0 && matches(settings[index]))
                        category_match = true;
                if (!category_match) continue;
                any_match = true;
                if(ui_tab || state.settings_filter.IsActive())ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                if (!ImGui::CollapsingHeader(window_title(categories[category], categories[category]).c_str())) continue;
                if (ImGui::BeginTable(categories[category], 2,
                                      ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
                    for (std::size_t index = 0; index < setting_count; ++index)
                        if (std::strcmp(settings[index].category, categories[category]) == 0 && matches(settings[index]))
                            draw_player_setting(config, settings[index], state.developer, state, resources.video_memory);
                    ImGui::EndTable();
                }
            }
            if(ui_tab && ImGui::CollapsingHeader(ui_text("Combat healthplates and effects")))state.combat_ui.draw_settings(config);
            if (!any_match) ImGui::TextDisabled(ui_text("No matching options."));
            }
            ImGui::EndChild();ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }

        if (ImGui::Button(ui_text("OK"), ImVec2(110.f * scale, 0.f))) accept = true;
        if (!state.settings_first_run) {
            ImGui::SameLine();
            if (ImGui::Button(ui_text("Cancel"), ImVec2(110.f * scale, 0.f))) cancel = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui_text(state.settings_first_run ? "OK saves your choice."
                                                     : "Changes show immediately. OK saves, Cancel or Esc reverts."));
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            (state.settings_first_run ? accept : cancel) = true;
        // Save warning: the previewed settings need more than the card's
        // local budget (enforced adapters only). Never blocks the save.
        const VramBudgetSnapshot& vram = resources.vram_budget;
        if (accept && vram.known && vram.enforced && vram.budget_bytes && vram.estimate_bytes() > vram.budget_bytes) {
            accept = false;
            state.vram_warning_need_mib = mib(vram.estimate_bytes());
            state.vram_warning_budget_mib = mib(vram.budget_bytes);
            ImGui::OpenPopup(window_title("Video memory", "video_memory").c_str());
        }
        if (ImGui::BeginPopupModal(window_title("Video memory", "video_memory").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(440.f * scale);
            ImGui::TextWrapped(ui_text("These settings need about %.0f MiB of video memory; this graphics card has %.0f MiB. "
                               "The game may stutter badly or the driver may reset. Save anyway?"),
                               state.vram_warning_need_mib, state.vram_warning_budget_mib);
            ImGui::PopTextWrapPos();
            if (ImGui::Button(ui_text("Save anyway"), ImVec2(130.f * scale, 0.f))) {
                accept = save_over_budget = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(ui_text("Go back"), ImVec2(130.f * scale, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    if (!keep_open) cancel = true;
    if (accept) {
        // Marker for the crash safety net; a save within the budget clears it.
        config.set_over_budget_pending(save_over_budget);
        if (save_over_budget) {
            char detail[96];
            std::snprintf(detail, sizeof(detail), "saved settings needing about %.0f MiB over a %.0f MiB budget",
                          state.vram_warning_need_mib, state.vram_warning_budget_mib);
            bridge_report_event("vram_budget_save_over", detail);
            install_quit_hook(state.window, state.config_path);
        }
    }
    if (accept) close_settings(state, config, true);
    else if (cancel) close_settings(state, config, false);
}

// The menu language follows the ui_language option every frame; Russian
// needs the Cyrillic font, so without one the menus stay English.
void apply_ui_language(DebugOverlay::Implementation& state, const RendererConfig& config) {
    set_ui_language(state.cyrillic_font ? configured_value(config, "ui_language") : 1);
    ImGui::GetIO().FontDefault = ui_russian() ? state.cyrillic_font : state.latin_font;
}

// The WDDM local video memory budget of the device's adapter (0: unknown).
std::uint64_t adapter_local_budget(ID3D11Device* device) noexcept {
    std::uint64_t budget = 0;
    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device)))) return 0;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgi_device->GetAdapter(&adapter))) {
        IDXGIAdapter3* adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3)))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) budget = local.Budget;
            adapter3->Release();
        }
        adapter->Release();
    }
    dxgi_device->Release();
    return budget;
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
                state.adapter_budget_bytes = local.Budget;
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

const char* video_memory_group_name(VideoMemoryGroup group) noexcept {
    switch (group) {
    case VideoMemoryGroup::screen: return "Screen buffers (resolution, render scale)";
    case VideoMemoryGroup::water: return "Water reflections and waves";
    case VideoMemoryGroup::ambient_occlusion: return "Ambient occlusion";
    case VideoMemoryGroup::fog: return "Volumetric fog";
    case VideoMemoryGroup::sky: return "Sky and clouds";
    case VideoMemoryGroup::sun_shadows: return "Sun shadows";
    case VideoMemoryGroup::lamp_shadows: return "Lamp and torch shadows";
    case VideoMemoryGroup::terrain_blend: return "Terrain blending, cliff and contact art";
    case VideoMemoryGroup::ground_cover: return "Grass, ground cover and ambient maps";
    case VideoMemoryGroup::critters: return "Critters";
    case VideoMemoryGroup::game_textures: return "Game textures (models, terrain, interface)";
    case VideoMemoryGroup::texture_arrays: return "Batched texture arrays";
    case VideoMemoryGroup::geometry: return "Geometry (models, terrain, neighbouring maps)";
    case VideoMemoryGroup::frame_uploads: return "Per-frame upload buffers";
    default: return "?";
    }
}

DebugOverlay::DebugOverlay() = default;

bool renderer_controls_capture_input(HWND window) noexcept {
    DWORD_PTR reference = 0;
    if (!GetWindowSubclass(window, overlay_subclass_proc, kOverlaySubclassId, &reference)) return false;
    const auto* overlay = reinterpret_cast<const DebugOverlay*>(reference);
    // Core mode: the game process's hook-only overlay takes the input for the host's.
    return overlay && overlay->input_ready() && overlay->captures_input();
}

DebugOverlay::~DebugOverlay() {
    shutdown();
}

void DebugOverlay::load_config_only(const wchar_t* config_path) {
    shutdown();
    const std::wstring path = config_path && *config_path ? std::wstring(config_path) : renderer_config_path();
    config_.load(path.c_str());
    set_ui_language(configured_value(config_, "ui_language"));
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
    set_ui_language(configured_value(config_, "ui_language"));
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
    // Crash safety net: settings saved past the VRAM budget and no clean exit
    // since (crash, hang, driver reset). Back to the adapter suggestion.
    if (config_.over_budget_pending()) {
        if (state->suggested_preset == GraphicsPreset::custom) suggest_for_adapter(*state, device);
        const GraphicsPreset preset =
            state->suggested_preset != GraphicsPreset::custom ? state->suggested_preset : GraphicsPreset::low;
        apply_graphics_preset(config_, preset);
        config_.set_over_budget_pending(false);
        if (config_.save(state->config_path.c_str())) config_.clear_dirty();
        std::snprintf(state->graphics_reset_text, sizeof(state->graphics_reset_text),
                      ui_text("The last session ended unexpectedly after choosing settings above this card's video memory. "
                              "Graphics were reset to %s; change them in F10."), ui_text(graphics_preset_name(preset)));
        state->graphics_reset_pending = true;
        char detail[96];
        std::snprintf(detail, sizeof(detail), "over_budget_pending was set at startup; graphics reset to %s",
                      graphics_preset_name(preset));
        bridge_report_event("vram_budget_reset", detail);
    }
    // The settings window's video memory line falls back to this budget while
    // the telemetry sampler is idle (it only runs with a developer overlay).
    // Only the first-run adapter suggestion set it, so a configured install
    // showed "Video memory: n/a" there.
    if (!state->adapter_budget_bytes) state->adapter_budget_bytes = adapter_local_budget(device);
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
    state->latin_font = io.Fonts->AddFontDefault();
    // Cyrillic: the built-in font is Latin only. The Windows UI font fills its
    // missing glyphs (game text such as effect names) and is the whole menu
    // font in Russian. Its glyphs are scaled up inside the unchanged line
    // height so they match the built-in font's visual size.
    if (const std::string cyrillic = system_ui_font_path(); !cyrillic.empty()) {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.ExtraSizeScale = kCyrillicGlyphScale;
        io.Fonts->AddFontFromFileTTF(cyrillic.c_str(), 0.f, &merge);
        ImFontConfig own;
        own.ExtraSizeScale = kCyrillicGlyphScale;
        state->cyrillic_font = io.Fonts->AddFontFromFileTTF(cyrillic.c_str(), 13.f, &own);
    }
    io.FontDefault = state->latin_font;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = state->ui_scale;
    style.ScaleAllSizes(state->ui_scale);
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;

    const bool platform = ImGui_ImplWin32_Init(window);
    if (!platform || !(renderer_ ? renderer_->init() : ImGui_ImplDX11_Init(device, context))) {
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        if (renderer_) renderer_->shutdown();
        else if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(state->imgui);
        return E_FAIL;
    }
    implementation_ = std::move(state);
    implementation_->combat_ui.initialize();
    if (initialize_host_child_) {
        implementation_->host_child = true;
        return S_OK;
    }
    if (!SetWindowSubclass(window, overlay_subclass_proc, kOverlaySubclassId,
                           reinterpret_cast<DWORD_PTR>(this))) {
        shutdown();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    bridge_set_camp_input_overlay(this);
    return S_OK;
}

bool DebugOverlay::input_ready() const noexcept { return implementation_ && !implementation_->host_child; }
void (*DebugOverlay::core_forward() const noexcept)(UINT, WPARAM, LPARAM) noexcept {
    return implementation_ ? implementation_->core_input.forward : nullptr;
}

HRESULT DebugOverlay::initialize_host(HWND window, ID3D11Device* device, ID3D11DeviceContext* context,
                                      const wchar_t* config_path) {
    initialize_host_child_ = true;
    const HRESULT hr = initialize(window, device, context, config_path);
    initialize_host_child_ = false;
    return hr;
}

void DebugOverlay::set_external_focus(bool focused) noexcept {
    if (implementation_) implementation_->external_focused = focused;
}

void DebugOverlay::process_forwarded_input(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (!implementation_ || !implementation_->imgui) return;
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
    ImGui::SetCurrentContext(implementation_->imgui);
    if (captures_input()) ImGui_ImplWin32_WndProcHandler(implementation_->window, message, wparam, lparam);
    else if (message == WM_MOUSEWHEEL) implementation_->combat_ui.process_input(message, wparam, lparam);
}

HRESULT DebugOverlay::initialize_core_input(HWND window, const CoreInputHooks& hooks) {
    if (implementation_ || !window || !hooks.flags || !hooks.forward) return E_INVALIDARG;
    auto state = std::make_unique<Implementation>();
    state->window = window;
    state->config_path = renderer_config_path();
    state->developer = renderer_developer_mode();
    state->core_input = hooks;
    implementation_ = std::move(state);
    if (!SetWindowSubclass(window, overlay_subclass_proc, kOverlaySubclassId, reinterpret_cast<DWORD_PTR>(this))) {
        implementation_.reset();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    bridge_set_camp_input_overlay(this);
    return S_OK;
}

void DebugOverlay::reload_config() noexcept {
    try {
        const std::wstring path = implementation_ ? implementation_->config_path : renderer_config_path();
        config_.load(path.c_str());
        config_.clear_dirty();
        set_ui_language(configured_value(config_, "ui_language"));
    } catch (...) {}
}

bool DebugOverlay::process_camp_input(UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if(!implementation_ || implementation_->host_child)return false;
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
    remove_quit_hook();
    if (!implementation_->host_child) {
        release_cursor_confinement(implementation_->cursor_confinement);
        if (GetCapture() == implementation_->window) ReleaseCapture();
        if (implementation_->window)
            RemoveWindowSubclass(implementation_->window, overlay_subclass_proc, kOverlaySubclassId);
    }
    if (implementation_->imgui) {
        ImGui::SetCurrentContext(implementation_->imgui);
        if (renderer_) renderer_->shutdown(); else ImGui_ImplDX11_Shutdown();
        // After the backend: it releases its copy of the combat icon atlas
        // (an ImGui user texture) before the atlas goes.
        implementation_->combat_ui.shutdown();
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
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
    if (!implementation_ || !implementation_->imgui) return E_UNEXPECTED;
    ImGui::SetCurrentContext(implementation_->imgui);
    maintain();
    const bool capture_notice = implementation_->capture_warmup || implementation_->capture_requested ||
        GetTickCount64() < implementation_->capture_notice_until;
    const bool combat_visible = implementation_->combat_ui.prepare(config_);
    if (!combat_visible && !visible() && resources.critters.labels.empty() && !notice_ && !capture_notice) return S_FALSE;

    apply_ui_language(*implementation_, config_);
    if (!renderer_) ImGui_ImplDX11_NewFrame();
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
    if (implementation_->settings_open) draw_settings_window(*implementation_, config_, resources);
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
                ImGui::TextUnformatted(ui_text("Capturing a diagnostic frame..."));
                const float done = implementation_->capture_warmup ? 1.f - implementation_->capture_warmup / 90.f : 1.f;
                ImGui::ProgressBar(done, ImVec2(-FLT_MIN, 0.f), "");
            } else {
                ImGui::TextUnformatted(ui_text("Frame captured."));
                ImGui::TextDisabled(ui_text("Saved in the EID3D11 captures folder."));
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

    if (renderer_) renderer_->render(ImGui::GetDrawData()); else ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    rendered_frame_ = true;
    return S_OK;
}

// Frame generation: the same draw lists once more onto the generated image
// (the overlay then shows on every presented frame, without flicker).
void DebugOverlay::render_again() noexcept {
    if (!std::exchange(rendered_frame_, false) || !renderer_ || !ImGui::GetCurrentContext()) return;
    if (ImDrawData* data = ImGui::GetDrawData(); data && data->Valid) renderer_->render(data);
}

void DebugOverlay::render_shader_preparation(std::size_t done, std::size_t total) noexcept {
    if (!implementation_ || !implementation_->imgui) return;
    ImGui::SetCurrentContext(implementation_->imgui);
    apply_ui_language(*implementation_, config_);
    if (!renderer_) ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    const float scale = implementation_->ui_scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(440.f * scale, io.DisplaySize.x * .9f), 0.f), ImGuiCond_Always);
    if (ImGui::Begin(window_title("Preparing shaders", "preparing_shaders").c_str(), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav)) {
        ImGui::TextWrapped(ui_text("Preparing the graphics for your settings."));
        char label[48];
        std::snprintf(label, sizeof(label), "%zu / %zu", done, total);
        ImGui::ProgressBar(total ? static_cast<float>(done) / static_cast<float>(total) : 1.f,
                           ImVec2(-FLT_MIN, 0.f), label);
    }
    ImGui::End();
    ImGui::Render();
    if (renderer_) renderer_->render(ImGui::GetDrawData()); else ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
    if(game_hud_editing()) {
        HudPanelBounds mission;
        if(!widescreen_hud_panel_bounds(HudPanel::messages,mission)) {
            set_game_hud_editing(false);implementation_->pending_save=true;
        }
    }
    const bool core_input = implementation_->core_input.flags != nullptr;
    const bool host_child = implementation_->host_child;
    const bool f10_down = !core_input && (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    const bool focused = host_child ? implementation_->external_focused : GetForegroundWindow() == implementation_->window;
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
    // Core mode: F11 goes to the host as its own command (core_present).
    const bool f11_down = !core_input && !host_child && (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
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
            if (!host_child && GetCapture() != implementation_->window) SetCapture(implementation_->window);
        } else if (implementation_->full_input_active) {
            for (std::size_t index = 0; index < implementation_->mouse_was_down.size(); ++index) {
                if (implementation_->mouse_was_down[index]) io.AddMouseButtonEvent(static_cast<int>(index), false);
                implementation_->mouse_was_down[index] = false;
            }
            if (!host_child && GetCapture() == implementation_->window) ReleaseCapture();
        }
    }
    implementation_->full_input_active = full_input;
    // The game window's own process confines the cursor (core mode: the game process).
    if (!host_child) {
        update_cursor_confinement(implementation_->cursor_confinement,
                                  implementation_->window, focused,
                                  cursor_confinement_option_enabled(config_));
        bridge_trace_input_focus(implementation_->window, "frame", 0, 0,
                                  static_cast<int>(config_.overlay_mode()));
    }
    if (g_quit_seen.load()) config_.set_over_budget_pending(false);  // cleared at WM_QUIT
    if (!implementation_->pending_save) return;
    // Core mode, game process: the host is the only ini writer; the options
    // and this request go to it with the next config command.
    if (core_input) {
        implementation_->pending_save = false;
        core_save_requested_ = true;
        return;
    }
    if (config_.save(implementation_->config_path.c_str())) {
        config_.clear_dirty();
        implementation_->pending_save = false;
        // Every saved interface change also lands in the current profile.
        save_profile_file(config_, current_profile_name(config_));
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
    if (implementation_ && implementation_->core_input.flags) return (implementation_->core_input.flags() & 1) != 0;
    return mode()==DebugOverlayMode::full || settings_open() ||
        game_hud_editing();
}
bool DebugOverlay::captures_input() const noexcept {return captures_keyboard();}

bool DebugOverlay::hud_editing() const noexcept {return game_hud_editing();}
float DebugOverlay::hud_grid_step() const noexcept {
    return hud_editing() && implementation_->hud_grid?24.f*implementation_->ui_scale:0.f;
}

unsigned DebugOverlay::samgbur_lod_instance() const noexcept {
    return implementation_?static_cast<unsigned>(std::max(0,implementation_->samgbur_lod_instance)):0u;
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

bool DebugOverlay::graphics_reset_notice_pending() const noexcept {
    return implementation_ && implementation_->graphics_reset_pending;
}

const char* DebugOverlay::graphics_reset_notice(bool world_on_screen) noexcept {
    if (!implementation_ || !implementation_->graphics_reset_pending) return nullptr;
    auto& state = *implementation_;
    const ULONGLONG now = GetTickCount64();
    if (!state.graphics_reset_until) {
        if (!world_on_screen) return nullptr;
        state.graphics_reset_until = now + 20000;
    }
    if (now < state.graphics_reset_until && !state.settings_open) return state.graphics_reset_text;
    state.graphics_reset_pending = false;
    return nullptr;
}

void DebugOverlay::add_graphics_reset_notice(const char* text) noexcept {
    if (!implementation_ || !text || !*text) return;
    auto& state = *implementation_;
    const std::size_t used = state.graphics_reset_pending ? std::strlen(state.graphics_reset_text) : 0;
    std::snprintf(state.graphics_reset_text + used, sizeof(state.graphics_reset_text) - used, "%s%s", used ? " " : "", text);
    state.graphics_reset_pending = true;
}

void DebugOverlay::sync_interface_profile() noexcept {
    if (!implementation_) return;
    // The current profile (and "default") always exists: a missing one is
    // written from the current settings, an existing one applied over them.
    const auto name = current_profile_name(config_);
    const auto path = profile_path(name);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && config_.load_interface_profile(path.c_str()))
        implementation_->pending_save = true;
    else save_profile_file(config_, name);
    if (name != "default" && GetFileAttributesW(profile_path("default").c_str()) == INVALID_FILE_ATTRIBUTES)
        save_profile_file(config_, "default");
}

void DebugOverlay::clear_over_budget_marker() noexcept {
    if (!implementation_ || !config_.over_budget_pending()) return;
    config_.set_over_budget_pending(false);
    if (config_.save(implementation_->config_path.c_str())) config_.clear_dirty();
    bridge_report_event("vram_budget_marker_cleared", "clean exit after an over-budget save");
}

const char* vram_budget_level_text(unsigned level) noexcept {
    switch (level) {
    case 0: return "none";
    case 1: return "textures 1/2";
    case 2: return "textures 1/2, shadows 1/2";
    default: return "textures 1/4, shadows 1/2";
    }
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
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
    if (!implementation_ || !implementation_->display_apply_pending) return false;
    settings = implementation_->display_apply;
    implementation_->display_apply_pending = false;
    return true;
}
void DebugOverlay::finish_display_apply(const DisplaySettings& settings, HRESULT result, const char* message) noexcept {
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
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

bool DebugOverlay::take_core_save_request() noexcept {
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
    const bool requested = core_save_requested_;
    core_save_requested_ = false;
    return requested;
}

void DebugOverlay::request_save() noexcept {
    std::lock_guard<std::recursive_mutex> guard(debug_overlay_lock());
    if (implementation_) implementation_->pending_save = true;
}

void DebugOverlay::set_mode(DebugOverlayMode mode) noexcept {
    config_.set_overlay_mode(mode);
    if (implementation_) implementation_->pending_save = true;
}

bool DebugOverlay::initialized() const noexcept {
    return implementation_ && implementation_->imgui;
}

void debug_overlay_set_running_renderer(const char* text, bool core) noexcept {
    g_running_renderer = text;
    g_running_core = core;
}

void debug_overlay_set_running_d3d12(bool running) noexcept {
    g_running_d3d12 = running;
}

void debug_overlay_set_game_address_space(std::uint32_t used_mb, std::uint32_t largest_free_mb) noexcept {
    g_core_game_va_used_mb.store(used_mb, std::memory_order_relaxed);
    g_core_game_va_free_mb.store(largest_free_mb, std::memory_order_relaxed);
}

} // namespace owned
