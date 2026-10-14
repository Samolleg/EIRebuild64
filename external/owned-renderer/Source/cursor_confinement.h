#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "debug_overlay_config.h"

namespace owned {

struct CursorConfinementState {
    bool active = false;
};

bool cursor_confinement_option_enabled(const RendererConfig& config) noexcept;
bool cursor_confinement_release_message(UINT message, WPARAM wparam) noexcept;
bool cursor_confinement_client_rect(HWND window, RECT& screen_rect) noexcept;
void update_cursor_confinement(CursorConfinementState& state, HWND window,
                               bool focused, bool enabled) noexcept;
void release_cursor_confinement(CursorConfinementState& state) noexcept;

// The game's cursor object (theCursor) and its camera input read and warp the
// OS cursor with GetCursorPos/SetCursorPos and use the values directly as
// game pixels, which only holds while the client starts at the screen origin
// (borderless on the primary monitor). Its call sites are redirected here;
// the values are mapped between screen and client coordinates of the game
// window and forwarded through the game's own import slots, so a later
// import hook still runs. Before the window is known they pass through.
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
struct GameCursorHooks {
    GetCursorPosFn* get_entry;   // what a redirected `call [imm32]` reads
    SetCursorPosFn* set_entry;
};
GameCursorHooks game_cursor_hooks(GetCursorPosFn* get_import, SetCursorPosFn* set_import) noexcept;
void set_game_cursor_window(HWND window) noexcept;
// Physical client pixels belong to Windows/ImGui; native pixels belong to
// the retained interface canvas. No conversion changes the OS display mode.
POINT game_output_to_native(HWND window, POINT point) noexcept;
POINT game_native_to_output(HWND window, POINT point) noexcept;
LPARAM game_native_mouse_message(UINT message, LPARAM value, bool screen_result = false) noexcept;
// Harness runs (EI_RENDERER_HARNESS_DIR set) drive the game without input. A
// cursor parked on or past a window edge made the game edge-scroll the
// camera away from a harness `camera` anchor, so in those runs the game reads
// the cursor at least an eighth of the client inside. In normal play the same
// inset applies only while another window is in front: the game then reads its
// last in-front cursor position (the pointer belongs to the other window, and
// a game left running would otherwise edge-scroll from it).
void set_game_cursor_harness(bool enabled) noexcept;
POINT harness_cursor_point(POINT client_point, LONG width, LONG height) noexcept;

} // namespace owned
