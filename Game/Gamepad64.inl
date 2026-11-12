// Host-thread adapter. Included once by GameMain.cpp, after NGame declarations.
#include "Gamepad64.h"
#include "GamepadState64.h"
#include "GamepadGestures64.h"
#include "GamepadMission64.h"
#include <controller_input_state.h>
#include "KeyMap.h"
#include "I3DScreenMission.h"
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

namespace Gamepad64 {
namespace {
Button keys[512], mouse[2];
bool dispatching = false, waitingNeutral = true;
unsigned generation = 0;
DWORD controller = XUSER_MAX_COUNT;
ULONGLONG lastPoll = 0, nextProbe = 0, lastClick = 0;
POINT clickPoint{};
double remainderX = 0, remainderY = 0, wheel = 0;
TimedPress leftPress, rightPress;
unsigned previousButtons = 0;
struct Picker {
    TriggerPicker input;
    int slot = -1;
    std::uint64_t token = 0;
};
Picker pickers[2];

void CancelPreviews() noexcept {
    auto* mission = dynamic_cast<CI3DScreenMission*>(theInterface.GetCurrentScreen());
    for (int side = 0; side < 2; ++side) {
        if (mission && pickers[side].slot >= 0)
            GamepadMissionAccess::Preview(*mission, side != 0, -1);
        if (pickers[side].slot >= 0 || pickers[side].input.selected) pickers[side].input.Abort();
        pickers[side].slot = -1;
    }
}

bool HasPadInput() noexcept {
    for (const auto& key : keys) if (key.pad) return true;
    return mouse[0].pad || mouse[1].pad;
}
UINT MouseFlags() noexcept {
    return (mouse[0].Down() ? MK_LBUTTON : 0) | (mouse[1].Down() ? MK_RBUTTON : 0) |
        ((keys[42].Down() || keys[54].Down()) ? MK_SHIFT : 0) |
        ((keys[29].Down() || keys[285].Down()) ? MK_CONTROL : 0);
}
void Emit(UINT message, WPARAM wParam, LPARAM lParam) {
    struct Guard {
        Guard() { dispatching = true; }
        ~Guard() { dispatching = false; }
    } guard;
    NGame::WndProc(message, wParam, lParam);
}
void WantAction(bool (&wanted)[512], EKbdActions action, bool down) noexcept {
    if (!down) return;
    for (int scan = 1; scan < TOTAL_NUM_SCANCODES; ++scan) {
        if (NKeyMap::keyMap[scan] == action) { wanted[scan] = true; return; }
    }
}
bool KeyDown(int scan) noexcept {
    const int partner = ModifierPartner(scan);
    return keys[scan].Down() || (partner >= 0 && keys[partner].Down());
}
void Key(int scan, bool down) {
    const bool before = KeyDown(scan);
    keys[scan].Set(true, down);
    owned::set_controller_modifiers(
        (keys[29].pad || keys[285].pad ? 1u : 0u) |
        (keys[56].pad || keys[312].pad ? 2u : 0u) |
        (keys[42].pad || keys[54].pad ? 4u : 0u));
    if (before == KeyDown(scan)) return;
    const UINT extendedScan = (scan & 255) | ((scan & 256) ? 0xe000 : 0);
    const UINT vk = MessageKey(MapVirtualKeyW(extendedScan, MAPVK_VSC_TO_VK_EX));
    const LPARAM flags = LPARAM(1) | (LPARAM(scan) << 16) |
        (down ? 0 : LPARAM(0xc0000000u));
    Emit(down ? WM_KEYDOWN : WM_KEYUP, vk, flags);
}
}

void Cancel() noexcept {
    CancelPreviews();
    leftPress = {}; rightPress = {}; previousButtons = 0;
    for (auto& picker : pickers) picker = {};
    const bool cancelDrag = mouse[0].pad || mouse[1].pad;
    for (auto& key : keys) key = {};
    owned::set_controller_modifiers(0);
    for (auto& button : mouse) button = {};
    waitingNeutral = true;
    remainderX = remainderY = wheel = 0;
    lastClick = 0;
    ++generation;
    if (cancelDrag) {
        // Camp overlays own drag/capture state separately from CInterface3D.
        // Cancel instead of sending button-up, which could drop/use an item.
        OwnedRender::E2CampInput(WM_CANCELMODE, 0, 0);
        OwnedRender::E4::Message(NGame::hWnd, WM_CANCELMODE, 0, 0);
    }
}

// Invoked only on the normal game message route. No SendInput/global keys.
bool PhysicalMessage(UINT message, WPARAM& wParam, LPARAM lParam) noexcept {
    if (dispatching) return true;
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN ||
        message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) CancelPreviews();
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN ||
        message == WM_KEYUP || message == WM_SYSKEYUP) {
        const int scan = (lParam >> 16) & 511;
        auto& key = keys[scan];
        const bool before = KeyDown(scan);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        key.Set(false, down);
        if (ModifierPartner(scan) >= 0) return before != KeyDown(scan);
        return !key.pad; // preserve all ordinary keyboard messages/repeats
    }
    int button = -1;
    bool down = false;
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) {
        button = 0; down = message != WM_LBUTTONUP;
    }
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK || message == WM_RBUTTONUP) {
        button = 1; down = message != WM_RBUTTONUP;
    }
    if (button >= 0) {
        const bool changed = mouse[button].Set(false, down);
        if (!changed && mouse[button].pad) return false;
    }
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
        wParam |= MouseFlags();
    return true;
}

void Poll() {
    const ULONGLONG now = GetTickCount64();
    if (now - lastPoll < 8) return;
    const double dt = (std::min)(0.05, (now - lastPoll) / 1000.0);
    lastPoll = now;
    if (AgentBackground::Enabled() || !NGame::hWnd || GetForegroundWindow() != NGame::hWnd ||
        IsIconic(NGame::hWnd) || !theInterface.GetCurrentScreen() ||
        ErrorDialog::Active() || OwnedRender::E3EditorPaused()) {
        if (HasPadInput()) theInterface.ResetInputState();
        else Cancel();
        return;
    }
    XINPUT_STATE state{};
    if (controller < XUSER_MAX_COUNT && XInputGetState(controller, &state) != ERROR_SUCCESS) {
        controller = XUSER_MAX_COUNT;
        if (HasPadInput()) theInterface.ResetInputState();
        else Cancel();
        nextProbe = now + 1000;
        return;
    }
    if (controller == XUSER_MAX_COUNT) {
        if (now < nextProbe) return;
        nextProbe = now + 1000;
        for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
            if (XInputGetState(index, &state) == ERROR_SUCCESS) { controller = index; break; }
        }
        if (controller == XUSER_MAX_COUNT) return;
        waitingNeutral = true;
    }
    const auto& pad = state.Gamepad;
    const Stick left = NormalizeStick(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    const Stick right = NormalizeStick(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    if (waitingNeutral) {
        waitingNeutral = pad.wButtons || left.x || left.y || right.x || right.y ||
            Trigger(pad.bLeftTrigger) || Trigger(pad.bRightTrigger);
        return;
    }
    RECT client{};
    POINT point{};
    if (!GetClientRect(NGame::hWnd, &client) || client.right <= 0 || client.bottom <= 0 ||
        !GetCursorPos(&point) || !ScreenToClient(NGame::hWnd, &point)) return;
    const unsigned epoch = generation;
    const bool precise = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    const double speed = client.bottom * (precise ? 0.25 : 0.9);
    remainderX += left.x * speed * dt;
    remainderY -= left.y * speed * dt;
    const int dx = int(remainderX), dy = int(remainderY);
    remainderX -= dx; remainderY -= dy;
    if (!left.x) remainderX = 0;
    if (!left.y) remainderY = 0;
    if (dx || dy) {
        point.x = (std::clamp)(point.x + dx, 0L, client.right - 1);
        point.y = (std::clamp)(point.y + dy, 0L, client.bottom - 1);
        POINT screen = point;
        if (ClientToScreen(NGame::hWnd, &screen)) SetCursorPos(screen.x, screen.y);
        // Refresh hover before a same-frame click. The queued OS move is
        // harmless (zero cursor delta) and still follows the normal route.
        Emit(WM_MOUSEMOVE, MouseFlags(), MAKELPARAM(point.x, point.y));
        if (generation != epoch) return;
    }
    // A click outside the client must not become an in-game command.
    const bool inside = PtInRect(&client, point) != FALSE;
    const LPARAM position = MAKELPARAM(point.x, point.y);
    bool wanted[512]{};
    wanted[1] = (pad.wButtons & XINPUT_GAMEPAD_START) != 0; // Escape in every screen
    wanted[42] = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0; // Shift
    wanted[56] = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0; // Alt
    const bool mission = !theInterface.IsConsoleVisible() &&
        dynamic_cast<CI3DScreenMission*>(theInterface.GetCurrentScreen()) != nullptr;
    if (mission) {
        WantAction(wanted, KA_PAUSE, pad.wButtons & XINPUT_GAMEPAD_X);
        WantAction(wanted, KA_MINIMAP, pad.wButtons & XINPUT_GAMEPAD_Y);
        WantAction(wanted, KA_OBJECTIVES, pad.wButtons & XINPUT_GAMEPAD_BACK);
        WantAction(wanted, KA_CAMERA_LEFT, right.x < -0.25);
        WantAction(wanted, KA_CAMERA_RIGHT, right.x > 0.25);
        WantAction(wanted, KA_CAMERA_UP, right.y > 0.25);
        WantAction(wanted, KA_CAMERA_DOWN, right.y < -0.25);
    }
    // Release before press when changing opposite camera directions.
    for (int scan = 1; scan < 512; ++scan) {
        if (keys[scan].pad && !wanted[scan]) Key(scan, false);
        if (generation != epoch) return;
    }
    // Modifiers first, before any command or click from this sample.
    if (wanted[42] && !keys[42].pad) Key(42, true);
    if (generation != epoch) return;
    for (int scan = 1; scan < 512; ++scan) {
        if (wanted[scan] && !keys[scan].pad) Key(scan, true);
        if (generation != epoch) return;
    }
    if (mission) {
        auto& screen = *static_cast<CI3DScreenMission*>(theInterface.GetCurrentScreen());
        const unsigned pressed = pad.wButtons & ~previousButtons;
        const unsigned old = previousButtons;
        previousButtons = pad.wButtons;
        const bool up = (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        const bool down = (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        if (up != down && (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)))
            GamepadMissionAccess::StepPose(screen, up ? 1 : -1);
        const bool leftHeld = (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        const bool rightHeld = (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        if (leftHeld && rightHeld) { leftPress = {}; rightPress = {}; }
        else {
            const auto l = leftPress.Update(leftHeld, now);
            const auto r = rightPress.Update(rightHeld, now);
            if (l == Press::shortPress) GamepadMissionAccess::CycleHero(screen, 1);
            if (l == Press::longPress) GamepadMissionAccess::CycleWeapon(screen);
            if (r == Press::shortPress) GamepadMissionAccess::CycleItem(screen);
            if (r == Press::longPress) GamepadMissionAccess::CycleHero(screen, -1);
        }
        constexpr unsigned sticks = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
        if ((pad.wButtons & sticks) == sticks && (old & sticks) != sticks && ModRuntime::Current())
            theCamera.ToggleFalloutMode();
        if (generation != epoch) return;
        for (int side = 0; side < 2; ++side) {
            auto& picker = pickers[side];
            const bool rightSide = side != 0;
            const bool bumper = (pad.wButtons & (rightSide ? XINPUT_GAMEPAD_RIGHT_SHOULDER : XINPUT_GAMEPAD_LEFT_SHOULDER)) != 0;
            const auto token = GamepadMissionAccess::Token(screen, rightSide);
            if (picker.slot >= 0 && picker.token != token) {
                GamepadMissionAccess::Preview(screen, rightSide, -1);
                picker.slot = -1;
                picker.input.Abort();
            }
            const auto event = picker.input.Update(bumper, rightSide ? pad.bRightTrigger : pad.bLeftTrigger);
            if (event == PickerEvent::advance) {
                const int current = picker.slot >= 0 ? picker.slot : GamepadMissionAccess::Selected(screen, rightSide);
                picker.slot = NextSlot(current, GamepadMissionAccess::Count(screen, rightSide));
                picker.token = token;
            }
            if (event == PickerEvent::commit) {
                const int slot = picker.slot;
                picker.slot = -1;
                GamepadMissionAccess::Commit(screen, rightSide, slot);
                if (generation != epoch) return;
            } else if (picker.slot >= 0) GamepadMissionAccess::Preview(screen, rightSide, picker.slot);
        }
    } else {
        CancelPreviews();
        for (auto& picker : pickers) picker = {};
        leftPress = {}; rightPress = {}; previousButtons = 0;
    }
    for (int button = 0; button < 2; ++button) {
        const bool down = inside && (pad.wButtons & (button ? XINPUT_GAMEPAD_B : XINPUT_GAMEPAD_A));
        if (down && !mouse[button].pad) CancelPreviews();
        if (!mouse[button].Set(true, down)) continue;
        UINT message = button ? (down ? WM_RBUTTONDOWN : WM_RBUTTONUP) :
            (down ? WM_LBUTTONDOWN : WM_LBUTTONUP);
        if (!button && down) {
            if (lastClick && now - lastClick <= GetDoubleClickTime() &&
                std::abs(point.x - clickPoint.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
                std::abs(point.y - clickPoint.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2) {
                message = WM_LBUTTONDBLCLK; lastClick = 0;
            } else { lastClick = now; clickPoint = point; }
        }
        Emit(message, MouseFlags(), position);
        if (generation != epoch) return;
    }
    const double wheelInput =
        (mission && pickers[1].input.BlocksWheel() ? 0 : Trigger(pad.bRightTrigger)) -
        (mission && pickers[0].input.BlocksWheel() ? 0 : Trigger(pad.bLeftTrigger));
    if (!wheelInput) wheel = 0;
    else wheel += wheelInput * 6.0 * dt;
    const int steps = int(wheel);
    if (steps && inside) {
        wheel -= steps;
        POINT screen = point;
        if (ClientToScreen(NGame::hWnd, &screen))
            Emit(WM_MOUSEWHEEL, MAKEWPARAM(MouseFlags(), short(steps * WHEEL_DELTA)), MAKELPARAM(screen.x, screen.y));
    }
}
}
