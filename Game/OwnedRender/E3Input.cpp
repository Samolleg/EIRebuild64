#include "E3Input.h"
#include "E3Cursor.h"
#include <cursor_confinement.h>
#include "game_focus.h"
#include "widescreen.h"
#include "hud_layout_context.h"
#include "../ErrorDialog.h"
#include "combat_ui.h"

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
    owned::embedded_hud_layout().editing = false;
}
}

namespace OwnedRender {
void E3PublishInterface(HWND next, unsigned w, unsigned h, bool active,
                        bool soleMission, void (*reset)() noexcept) noexcept {
    const bool nextRunning = active && next && w && h;
    if (window != next || !nextRunning || !soleMission) {
        endEditing();
        owned::embedded_hud_layout().retire();
        owned::widescreen::hud_blit = {};
    }
    window = next; width = w; height = h;
    running = nextRunning;
    owned::set_embedded_cursor_canvas(next, nextRunning ? w : 0, nextRunning ? h : 0, &ErrorDialog::Active);
    E3CursorCanvas(next, w, h, nextRunning);
    mission = running && soleMission;
    if (!mission) owned::combat_ui_publish_hover(0, 0);
    resetInput = reset;
    // Match both c56 consumers: canvas_width/height query the function, while
    // native geometry and 2D surface composition use this shared state.
    owned::widescreen::interface_owned = running;
    if (running) owned::embedded_hud_layout().publish_canvas(w, h);
}
void E3RetireInterface() noexcept {
    E3CursorStop();
    owned::set_embedded_cursor_canvas(nullptr, 0, 0, nullptr);
    owned::embedded_hud_layout().retire();
    endEditing();
    window = nullptr; width = height = 0;
    running = mission = false; resetInput = nullptr;
    owned::combat_ui_publish_hover(0, 0);
    owned::widescreen::interface_owned = false;
    owned::widescreen::hud_blit = {};
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
bool E3HudRendererAttached() noexcept { return running && mission && sinkOwner && frameSink; }
void E3PublishHover(std::int32_t id, std::uint64_t incarnation) noexcept {
    owned::combat_ui_publish_hover(running && mission ? id : 0, running && mission ? incarnation : 0);
}
void E3HudBoundary() noexcept {
    if (running && frameSink) frameSink(sinkOwner);
}
}

namespace owned {
// Direct host ownership of the interface canvas, not ownership of the D3D
// device or the world frame. The latter is established by the renderer startup.
bool widescreen_interface_owned() noexcept { return running; }
bool game_hud_editor_available() noexcept { return mission && resetInput; }
bool game_hud_editing() noexcept { return editing; }
void set_game_hud_editing(bool enabled) noexcept {
    enabled = enabled && game_hud_editor_available();
    if (enabled == editing) return;
    if (resetInput) resetInput();
    editing = enabled;
    embedded_hud_layout().editing = enabled;
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
