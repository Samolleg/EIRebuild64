#include "E3Input.h"
#include "game_focus.h"
#include "widescreen.h"
#include "../ErrorDialog.h"

namespace {
HWND window = nullptr;
unsigned width = 0, height = 0;
bool running = false, mission = false, editing = false;
void (*resetInput)() noexcept = nullptr;
void* sinkOwner = nullptr;
void (*frameSink)(void*) noexcept = nullptr;

void endEditing() noexcept {
    if (editing && resetInput) resetInput();
    editing = false;
}
}

namespace OwnedRender {
void E3PublishInterface(HWND next, unsigned w, unsigned h, bool active,
                        bool soleMission, void (*reset)() noexcept) noexcept {
    if (window != next || !active || !soleMission) endEditing();
    window = next; width = w; height = h;
    running = active && next && w && h;
    mission = running && soleMission;
    resetInput = reset;
}
void E3RetireInterface() noexcept {
    endEditing();
    window = nullptr; width = height = 0;
    running = mission = false; resetInput = nullptr;
    // Sink belongs to the renderer, not a particular mission. It is cleared
    // at interface teardown; mission transitions preserve its registration.
    sinkOwner = nullptr; frameSink = nullptr;
}
HWND E3Window() noexcept { return window; }
bool E3Canvas(unsigned& w, unsigned& h) noexcept {
    w = width; h = height;
    return running && width && height;
}
bool E3EditorPaused() noexcept { return editing; }
bool E3BlockInput(UINT message, WPARAM parameter) noexcept {
    // Closing the application must remain possible while editing.
    if (message == WM_SYSKEYDOWN && parameter == VK_F4) return false;
    const bool input = (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == 0xcc54;
    return input && (editing || (running && GetForegroundWindow() != window));
}
void E3HudBoundary() noexcept {
    if (running && frameSink) frameSink(sinkOwner);
}
}

namespace owned {
bool game_hud_editor_available() noexcept { return mission && resetInput; }
bool game_hud_editing() noexcept { return editing; }
void set_game_hud_editing(bool enabled) noexcept {
    enabled = enabled && game_hud_editor_available();
    if (enabled == editing) return;
    if (resetInput) resetInput();
    editing = enabled;
}
bool game_focus_swallow_activation(HWND target, UINT message, WPARAM) noexcept {
    if (message != WM_ACTIVATE || target != window || !running ||
        !resetInput || ErrorDialog::Active()) return false;
    resetInput();
    return true;
}
void widescreen_set_hud_frame_sink(void* owner, void (*sink)(void*) noexcept) noexcept {
    sinkOwner = owner; frameSink = sink;
}
void widescreen_clear_hud_frame_sink(void* owner) noexcept {
    if (sinkOwner == owner) { sinkOwner = nullptr; frameSink = nullptr; }
}
}
