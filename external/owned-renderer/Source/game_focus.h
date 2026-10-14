#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace owned {

// What the game does while its window is not in front (option
// "background_mode"). The game was written for exclusive fullscreen: on
// WM_ACTIVATE WA_INACTIVE its window procedure stops drawing and the world,
// pauses sound, unacquires the mouse and minimises its window; on the way back
// it restores every surface, recreates and reloads every loaded texture and
// reloads the cursor.
// Neither mode lets it take that path once it is running:
//   keep_running: the world, the interface and the frames go on; Present is
//                 capped by background_frame_limit;
//   pause:        the world clock stops and no frame is drawn, so the window
//                 keeps showing the last presented frame.
// In both, keyboard and mouse messages and the cursor position stop reaching
// the interface while another window is in front.
enum class BackgroundMode : int { keep_running = 0, pause = 1 };

void set_game_focus_window(HWND window) noexcept;
void set_background_mode(int mode) noexcept;
// Mission HUD editor: stop gameplay ticks/input, retain UI idle and drawing.
bool game_hud_editor_available() noexcept;
bool game_hud_editing() noexcept;
void set_game_hud_editing(bool enabled) noexcept;

// Installs the three WinMain loop call-site hooks (world clock, message
// handler, frame) and enables the activation filter. Called once on the game's
// main thread before its message loop starts; sites_ok is the file site check.
void install_game_focus(bool sites_ok) noexcept;

// Restore preceding call owners before EngineBridge teardown. On a foreign
// write or failed restoration, retain the journal and forwarders for retry.
bool uninstall_game_focus() noexcept;

// For the game window's subclass: true when this WM_ACTIVATE must not reach
// the game (it is already running and would take its lost-focus path).
bool game_focus_swallow_activation(HWND window, UINT message, WPARAM wparam) noexcept;

// True when the game window is in front (or not known yet).
bool game_window_in_front() noexcept;

} // namespace owned
