// Console-only test: USER32 operations below are deterministic substitutes.
// No game, window, cursor clipping or pointer movement is performed.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "cursor_confinement.h"
#include "game_focus.h"
#include "widescreen.h"
#include "../ErrorDialog.h"

namespace check {
HWND host = reinterpret_cast<HWND>(1), foreground = host;
RECT client{0, 0, 1600, 900}, clipped{};
POINT origin{-1700, 100}, mouse{-900, 550}, written{};
bool visible = true, iconic = false, valid = true, clipSuccess = true;
unsigned clipCalls = 0, releaseCalls = 0, warpCalls = 0, resets = 0, frames = 0, assertions = 0;
HWND front() { return foreground; }
BOOL rect(HWND w, RECT* r) { if (!valid || w != host) return FALSE; *r = client; return TRUE; }
BOOL toScreen(HWND w, POINT* p) { if (!valid || w != host) return FALSE; p->x += origin.x; p->y += origin.y; return TRUE; }
BOOL toClient(HWND w, POINT* p) { if (!valid || w != host) return FALSE; p->x -= origin.x; p->y -= origin.y; return TRUE; }
BOOL isVisible(HWND) { return visible; }
BOOL isIconic(HWND) { return iconic; }
BOOL clip(const RECT* r) { if (r) { ++clipCalls; clipped = *r; } else ++releaseCalls; return clipSuccess; }
BOOL get(POINT* p) { *p = mouse; return TRUE; }
BOOL set(int x, int y) { ++warpCalls; written = {x, y}; return TRUE; }
void reset() noexcept { ++resets; }
void frame(void*) noexcept { ++frames; }
void require(bool condition, const char* name) { ++assertions; if (!condition) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); } }
}
#define GetForegroundWindow check::front
#define GetClientRect check::rect
#define ClientToScreen check::toScreen
#define ScreenToClient check::toClient
#define IsWindowVisible check::isVisible
#define IsIconic check::isIconic
#define ClipCursor check::clip
#define GetCursorPos check::get
#define SetCursorPos check::set
#include "E3Input.cpp"
#include "E3Cursor.cpp"
#include "E3Scale.cpp"

int main() {
    using namespace check;
    using namespace owned;
    using namespace OwnedRender;
    require(!game_hud_editor_available(), "pre-interface editor unavailable");
    set_game_hud_editing(true);
    require(!game_hud_editing(), "pre-interface enable rejected");
    require(game_native_mouse_message(WM_MOUSEMOVE, 77, false) == 77, "pre-interface mouse passthrough");
    E3PublishInterface(host, 800, 600, true, false, reset);
    require(!game_hud_editor_available(), "menu editor unavailable");
    E3PublishInterface(host, 800, 600, true, true, reset);
    require(game_hud_editor_available(), "mission editor available");
    set_game_hud_editing(true);
    require(game_hud_editing() && resets == 1 && E3EditorPaused(), "editor pauses and resets");
    set_game_hud_editing(true);
    require(resets == 1, "idempotent editor state");
    require(E3BlockInput(WM_LBUTTONDOWN, 0) && E3BlockInput(WM_KEYUP, 'N'), "editor blocks gameplay input");
    require(!E3BlockInput(WM_SYSKEYDOWN, VK_F4) && !E3BlockInput(WM_CLOSE, 0), "editor permits closing");
    E3PublishInterface(host, 800, 600, true, false, reset);
    require(!game_hud_editing() && resets == 2, "modal exits editing");
    require(game_focus_swallow_activation(host, WM_ACTIVATE, WA_INACTIVE) && resets == 3, "focus reset");
    require(!game_focus_swallow_activation(nullptr, WM_ACTIVATE, 0), "foreign window rejected");
    require(!game_focus_swallow_activation(host, WM_SIZE, 0), "non-activation rejected");
    ErrorDialog::State error{}; ErrorDialog::current = &error;
    require(!game_focus_swallow_activation(host, WM_ACTIVATE, 0), "R43 activation untouched");
    ErrorDialog::current = nullptr;
    foreground = nullptr;
    require(E3BlockInput(WM_MOUSEMOVE, 0) && E3BlockInput(0xcc54, 0), "background input blocked");
    foreground = host;
    require(!E3BlockInput(WM_KEYDOWN, 'N'), "foreground N preserved");
    for (int x : {-1600, -1, 0, 300, 800, 1599, 3200})
        for (int y : {-900, -1, 0, 450, 899, 1800}) {
            POINT p = game_output_to_native(host, {x, y});
            require(p.x == LONG(std::lround(x * .5)) && p.y == LONG(std::lround(y * 2. / 3.)), "canvas mapping");
            LPARAM packed = game_native_mouse_message(WM_MOUSEMOVE, MAKELPARAM(WORD(x), WORD(y)), false);
            require(short(LOWORD(packed)) == p.x && short(HIWORD(packed)) == p.y, "signed message mapping");
        }
    LPARAM wheel = game_native_mouse_message(WM_MOUSEWHEEL, MAKELPARAM(WORD(-900), WORD(550)), false);
    require(short(LOWORD(wheel)) == 400 && short(HIWORD(wheel)) == 300, "wheel screen-to-client once");
    wheel = game_native_mouse_message(WM_MOUSEWHEEL, MAKELPARAM(WORD(-900), WORD(550)), true);
    require(short(LOWORD(wheel)) == -1300 && short(HIWORD(wheel)) == 400, "camp screen-result contract");
    require(game_native_mouse_message(WM_KEYDOWN, 1234, false) == 1234, "keyboard passthrough");
    POINT p{}; E3ReadCursor(&p);
    require(p.x == 400 && p.y == 300, "polled pointer uses same canvas");
    E3WriteCursor(p.x, p.y);
    require(written.x == mouse.x && written.y == mouse.y, "warp inverse mapping");
    foreground = nullptr;
    require(!E3WriteCursor(1, 1) && warpCalls == 1, "no background warp");
    mouse = {-9999, -9999}; E3ReadCursor(&p);
    require(p.x == 400 && p.y == 300, "background pointer stays at last foreground point");
    foreground = host;
    CursorConfinementState state{};
    update_cursor_confinement(state, host, true, true);
    require(state.active && clipped.left == -1700 && clipped.top == 100 && clipped.right == -100, "client screen rectangle");
    ErrorDialog::current = &error;
    update_cursor_confinement(state, host, true, true);
    require(!state.active && releaseCalls == 1, "R43 releases cursor");
    update_cursor_confinement(state, host, true, true);
    require(clipCalls == 1 && releaseCalls == 1, "R43 no reacquire");
    ErrorDialog::current = nullptr;
    for (unsigned reason = 0; reason != 6; ++reason) {
        state.active = true;
        foreground = reason == 0 ? nullptr : host;
        visible = reason != 1; iconic = reason == 2; valid = reason != 3;
        const unsigned before = releaseCalls;
        update_cursor_confinement(state, host, reason != 4, reason != 5);
        require(!state.active && releaseCalls == before + 1, "release on invalid state");
    }
    valid = visible = true; iconic = false; foreground = host;
    for (UINT message : {WM_KILLFOCUS, WM_CANCELMODE, WM_DESTROY, WM_NCDESTROY})
        require(cursor_confinement_release_message(message, 0), "release message");
    require(cursor_confinement_release_message(WM_SIZE, SIZE_MINIMIZED) &&
        !cursor_confinement_release_message(WM_SIZE, SIZE_RESTORED), "minimized release only");
    require(cursor_confinement_release_message(WM_ACTIVATE, WA_INACTIVE) &&
        !cursor_confinement_release_message(WM_ACTIVATE, WA_ACTIVE), "activation release");
    RendererConfig config;
    require(!cursor_confinement_option_enabled(config), "missing option disabled");
    require(config.register_boolean("confine_cursor", "Confine cursor", "Input", true), "register existing option fixture");
    require(cursor_confinement_option_enabled(config), "existing option enabled");
    config.set_boolean("confine_cursor", false);
    require(!cursor_confinement_option_enabled(config), "existing option disabled");
    widescreen_set_hud_frame_sink(host, frame);
    widescreen_clear_hud_frame_sink(nullptr);
    E3HudBoundary(); require(frames == 1, "foreign owner cannot clear sink");
    widescreen_clear_hud_frame_sink(host);
    E3HudBoundary(); require(frames == 1, "owner clears sink");
    widescreen_set_hud_frame_sink(host, frame);
    E3RetireInterface(); E3HudBoundary();
    require(frames == 1 && !game_hud_editor_available() && !E3EditorPaused(), "teardown clears lifetime");
    require(E3DrawRadius(123.f, 1920, 1080) == 123.f, "host radius preserved before renderer setting");
    widescreen_set_landscape_radius(600.f);
    require(E3DrawRadius(123.f, 1920, 1080) == 600.f, "renderer radius used by host");
    widescreen_set_landscape_radius(99.f);
    require(E3DrawRadius(123.f, 800, 600) == 500.f, "renderer radius minimum");
    widescreen_set_landscape_radius(5000.f);
    require(E3DrawRadius(123.f, 800, 600) == 1000.f && E3DrawRadius(123.f, 3840, 1080) > 1000.f, "radius cap and wide square");
    for (int percent : {-1, 50, 100, 200, 300, 400}) {
        widescreen_set_combat_text_scale(percent);
        float vertices[2][4] = {{110.f, 80.f, .25f, .75f}, {90.f, 120.f, .5f, .1f}};
        E3CombatVertices(vertices, 2, sizeof(vertices[0]));
        const float expected = std::clamp(percent, 50, 300) / 100.f;
        require(vertices[0][0] == 100.f + 10.f * expected && vertices[0][1] == 100.f - 20.f * expected &&
            vertices[1][0] == 100.f - 10.f * expected && vertices[1][1] == 100.f + 20.f * expected &&
            vertices[0][2] == .25f && vertices[0][3] == .75f && vertices[1][2] == .5f && vertices[1][3] == .1f,
            "combat scale around group bounds preserves payload");
    }
    std::printf("PASS E3 input/cursor/frame sink/scale: %u assertions; no real USER32 side effects\n", assertions);
}
