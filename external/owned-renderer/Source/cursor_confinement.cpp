#include "cursor_confinement.h"

#include "game_focus.h"
#include "widescreen.h"
#include <cmath>

namespace owned {

bool cursor_confinement_option_enabled(const RendererConfig& config) noexcept {
    const RendererOption* option = config.find("confine_cursor");
    return option && option->type == RendererOptionType::boolean &&
           option->boolean_value;
}

bool cursor_confinement_release_message(UINT message, WPARAM wparam) noexcept {
    switch (message) {
    case WM_ACTIVATEAPP:
        return wparam == FALSE;
    case WM_ACTIVATE:
        return LOWORD(wparam) == WA_INACTIVE;
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
    case WM_DESTROY:
    case WM_NCDESTROY:
        return true;
    case WM_SIZE:
        return wparam == SIZE_MINIMIZED;
    case WM_SHOWWINDOW:
        return wparam == FALSE;
    default:
        return false;
    }
}

bool cursor_confinement_client_rect(HWND window, RECT& screen_rect) noexcept {
    if (!window) return false;
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    POINT upper_left{client.left, client.top};
    POINT lower_right{client.right, client.bottom};
    if (!ClientToScreen(window, &upper_left) ||
        !ClientToScreen(window, &lower_right)) return false;
    if (lower_right.x <= upper_left.x || lower_right.y <= upper_left.y)
        return false;
    screen_rect = {upper_left.x, upper_left.y, lower_right.x, lower_right.y};
    return true;
}

namespace {
GetCursorPosFn* g_get_import = nullptr;
SetCursorPosFn* g_set_import = nullptr;
HWND volatile g_game_window = nullptr;
bool g_harness = false;
POINT g_front_point{};
bool g_have_front_point = false;

BOOL WINAPI game_get_cursor_pos(LPPOINT point) {
    if (!g_get_import || !*g_get_import) return FALSE;
    const BOOL result = (*g_get_import)(point);
    if (result && point && g_game_window) {
        ScreenToClient(g_game_window, point);
        RECT client{};
        if (g_harness) {
            if (GetClientRect(g_game_window, &client))
                *point = harness_cursor_point(*point, client.right, client.bottom);
        } else if (game_window_in_front()) {
            g_front_point = *point;
            g_have_front_point = true;
        } else {
            // Another window is in front and the game may still be drawing
            // (background_mode): the pointer belongs to that window. The game
            // reads where its cursor was last, kept off the edge-scroll band
            // (its mission screens poll the cursor every drawn frame).
            if (g_have_front_point) *point = g_front_point;
            if (GetClientRect(g_game_window, &client))
                *point = harness_cursor_point(*point, client.right, client.bottom);
        }
    }
    if (result && point && g_game_window) *point = game_output_to_native(g_game_window, *point);
    return result;
}

BOOL WINAPI game_set_cursor_pos(int x, int y) {
    if (!g_set_import || !*g_set_import) return FALSE;
    POINT point{x, y};
    if (g_game_window) {
        point = game_native_to_output(g_game_window, point);
        ClientToScreen(g_game_window, &point);
    }
    return (*g_set_import)(point.x, point.y);
}

GetCursorPosFn g_get_entry = &game_get_cursor_pos;
SetCursorPosFn g_set_entry = &game_set_cursor_pos;
} // namespace

GameCursorHooks game_cursor_hooks(GetCursorPosFn* get_import, SetCursorPosFn* set_import) noexcept {
    g_get_import = get_import;
    g_set_import = set_import;
    return {&g_get_entry, &g_set_entry};
}

void set_game_cursor_window(HWND window) noexcept {
    g_game_window = window;
    set_game_focus_window(window);
}

POINT game_output_to_native(HWND window, POINT point) noexcept {
    RECT client{};
    if (widescreen_interface_owned() && GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
        const auto& canvas = widescreen_layout();
        point.x = LONG(std::lround(point.x * canvas.width / client.right));
        point.y = LONG(std::lround(point.y * canvas.height / client.bottom));
    }
    return point;
}
POINT game_native_to_output(HWND window, POINT point) noexcept {
    RECT client{};
    if (widescreen_interface_owned() && GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
        const auto& canvas = widescreen_layout();
        point.x = LONG(std::lround(point.x * client.right / canvas.width));
        point.y = LONG(std::lround(point.y * client.bottom / canvas.height));
    }
    return point;
}
LPARAM game_native_mouse_message(UINT message, LPARAM value, bool screen_result) noexcept {
    if (!g_game_window || !widescreen_interface_owned()) return value;
    switch (message) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: case WM_MOUSEWHEEL:
        break;
    default: return value;
    }
    POINT point{LONG(short(LOWORD(value))), LONG(short(HIWORD(value)))};
    if (message == WM_MOUSEWHEEL) ScreenToClient(g_game_window, &point);
    point = game_output_to_native(g_game_window, point);
    // Camp's wheel entry consumes an OS screen point; the native dispatcher
    // passes its POINT directly to the interface. Each gets its own copy.
    if (message == WM_MOUSEWHEEL && screen_result) ClientToScreen(g_game_window, &point);
    return MAKELPARAM(WORD(point.x), WORD(point.y));
}

void set_game_cursor_harness(bool enabled) noexcept { g_harness = enabled; }

POINT harness_cursor_point(POINT p, LONG width, LONG height) noexcept {
    const LONG mx = width / 8, my = height / 8;
    if (width > 0) p.x = p.x < mx ? mx : p.x > width - 1 - mx ? width - 1 - mx : p.x;
    if (height > 0) p.y = p.y < my ? my : p.y > height - 1 - my ? height - 1 - my : p.y;
    return p;
}

void release_cursor_confinement(CursorConfinementState& state) noexcept {
    if (!state.active) return;
    ClipCursor(nullptr);
    state.active = false;
}

void update_cursor_confinement(CursorConfinementState& state, HWND window,
                               bool focused, bool enabled) noexcept {
    if (!focused || !enabled || !window || !IsWindowVisible(window) ||
        IsIconic(window)) {
        release_cursor_confinement(state);
        return;
    }
    RECT screen{};
    if (!cursor_confinement_client_rect(window, screen)) {
        release_cursor_confinement(state);
        return;
    }
    if (ClipCursor(&screen)) {
        state.active = true;
        return;
    }
    release_cursor_confinement(state);
}

} // namespace owned
