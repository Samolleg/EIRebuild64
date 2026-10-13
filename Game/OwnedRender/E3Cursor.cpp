#include "E3Input.h"
#include "cursor_confinement.h"
#include "../ErrorDialog.h"
#include <cmath>
#include <algorithm>

namespace OwnedRender {
BOOL E3ReadCursor(POINT* point) noexcept {
    if (!point || !GetCursorPos(point)) return FALSE;
    const HWND window = E3Window();
    if (!window) return TRUE;
    if (!ScreenToClient(window, point)) return FALSE;
    static HWND lastWindow = nullptr;
    static POINT last{};
    RECT client{};
    if (GetForegroundWindow() == window) { last = *point; lastWindow = window; }
    else if (GetClientRect(window, &client)) {
        *point = lastWindow == window ? last : POINT{client.right / 2, client.bottom / 2};
        if (client.right > 0) point->x = std::clamp(point->x, client.right / 8, client.right - 1 - client.right / 8);
        if (client.bottom > 0) point->y = std::clamp(point->y, client.bottom / 8, client.bottom - 1 - client.bottom / 8);
    }
    *point = owned::game_output_to_native(window, *point);
    return TRUE;
}
BOOL E3WriteCursor(int x, int y) noexcept {
    HWND window = E3Window();
    if (!window) return SetCursorPos(x, y);
    if (GetForegroundWindow() != window || ErrorDialog::Active()) return FALSE;
    unsigned width = 0, height = 0;
    RECT client{};
    POINT point{x, y};
    if (E3Canvas(width, height) && GetClientRect(window, &client) &&
        client.right > 0 && client.bottom > 0) {
        point.x = LONG(std::lround(double(x) * client.right / width));
        point.y = LONG(std::lround(double(y) * client.bottom / height));
    }
    if (!ClientToScreen(window, &point)) return FALSE;
    return SetCursorPos(point.x, point.y);
}
}

namespace owned {
bool cursor_confinement_option_enabled(const RendererConfig& config) noexcept {
    const auto* option = config.find("confine_cursor");
    return option && option->type == RendererOptionType::boolean && option->boolean_value;
}
bool cursor_confinement_release_message(UINT message, WPARAM parameter) noexcept {
    switch (message) {
    case WM_ACTIVATEAPP: case WM_SHOWWINDOW: return parameter == FALSE;
    case WM_ACTIVATE: return LOWORD(parameter) == WA_INACTIVE;
    case WM_SIZE: return parameter == SIZE_MINIMIZED;
    case WM_KILLFOCUS: case WM_CANCELMODE: case WM_DESTROY: case WM_NCDESTROY: return true;
    default: return false;
    }
}
bool cursor_confinement_client_rect(HWND window, RECT& result) noexcept {
    RECT rect{};
    if (!window || !GetClientRect(window, &rect)) return false;
    POINT first{rect.left, rect.top}, last{rect.right, rect.bottom};
    if (!ClientToScreen(window, &first) || !ClientToScreen(window, &last) ||
        last.x <= first.x || last.y <= first.y) return false;
    result = {first.x, first.y, last.x, last.y};
    return true;
}
void release_cursor_confinement(CursorConfinementState& state) noexcept {
    if (!state.active) return;
    ClipCursor(nullptr);
    state.active = false;
}
void update_cursor_confinement(CursorConfinementState& state, HWND window,
                               bool focused, bool enabled) noexcept {
    RECT rect{};
    // R43: do not re-acquire from the renderer subclass during an error dialog.
    // An out-of-date 'focused' argument cannot capture another application's cursor.
    if (!focused || !enabled || !window || GetForegroundWindow() != window ||
        ErrorDialog::Active() || !IsWindowVisible(window) || IsIconic(window) ||
        !cursor_confinement_client_rect(window, rect)) {
        release_cursor_confinement(state);
        return;
    }
    if (ClipCursor(&rect)) state.active = true;
    else release_cursor_confinement(state);
}
POINT game_output_to_native(HWND window, POINT point) noexcept {
    unsigned width = 0, height = 0;
    RECT client{};
    if (window == OwnedRender::E3Window() && OwnedRender::E3Canvas(width, height) &&
        GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
        point.x = LONG(std::lround(double(point.x) * width / client.right));
        point.y = LONG(std::lround(double(point.y) * height / client.bottom));
    }
    return point;
}
LPARAM game_native_mouse_message(UINT message, LPARAM value, bool screenResult) noexcept {
    HWND window = OwnedRender::E3Window();
    if (!window) return value;
    switch (message) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: case WM_MOUSEWHEEL:
        break;
    default: return value;
    }
    POINT point{LONG(short(LOWORD(value))), LONG(short(HIWORD(value)))};
    if (message == WM_MOUSEWHEEL && !ScreenToClient(window, &point)) return value;
    point = game_output_to_native(window, point);
    if (message == WM_MOUSEWHEEL && screenResult && !ClientToScreen(window, &point)) return value;
    return MAKELPARAM(WORD(point.x), WORD(point.y));
}
}
