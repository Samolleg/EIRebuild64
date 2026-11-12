#pragma once
#include "GamepadState64.h"
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

namespace Gamepad64 {
// Movie playback has its own message pump, including before a UI screen exists.
// It consumes button edges directly, never synthesizing clicks into that screen.
class MovieInput {
    MovieButtonGate gates[XUSER_MAX_COUNT];
    ULONGLONG nextPoll = 0;
public:
    bool SkipPressed(bool active, bool skippable) noexcept {
        if (!active) {
            for (auto& gate : gates) gate.Update(false, 0, false);
            nextPoll = 0;
            return false;
        }
        const auto now = GetTickCount64();
        if (now < nextPoll) return false;
        nextPoll = now + 16;
        bool pressed = false;
        for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
            XINPUT_STATE state{};
            const bool available = XInputGetState(index, &state) == ERROR_SUCCESS;
            // Defined digital buttons only; guide/reserved bits are not input.
            const unsigned buttons = state.Gamepad.wButtons & 0xf3ffu;
            pressed = gates[index].Update(available, buttons, skippable) || pressed;
        }
        return pressed;
    }
};
}
