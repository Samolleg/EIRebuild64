#pragma once
#include <cmath>

namespace Gamepad64 {
// Each movie/controller needs an observed release before accepting a press.
// Inactive/disconnected samples disarm it; forbidden skips still consume edges.
struct MovieButtonGate {
    bool armed = false;
    unsigned previous = 0;
    bool Update(bool available, unsigned buttons, bool skippable) noexcept {
        if (!available) { armed = false; previous = 0; return false; }
        if (!armed) { armed = buttons == 0; previous = buttons; return false; }
        const bool pressed = (buttons & ~previous) != 0;
        previous = buttons;
        return skippable && pressed;
    }
};
struct Stick { double x = 0, y = 0; };
inline Stick NormalizeStick(short x, short y, int deadzone) noexcept {
    const double length = std::sqrt(double(x) * x + double(y) * y);
    if (length <= deadzone) return {};
    const double magnitude = ((length < 32767 ? length : 32767) - deadzone) / (32767 - deadzone);
    return {x / length * magnitude, y / length * magnitude};
}
inline double Trigger(unsigned char value) noexcept {
    return value <= 30 ? 0 : (value - 30) / 225.0;
}
// Physical and controller input share ownership: releasing either source
// must not release a button still held by the other.
struct Button {
    bool physical = false, pad = false;
    bool Down() const noexcept { return physical || pad; }
    bool Set(bool controller, bool value) noexcept {
        const bool before = Down();
        (controller ? pad : physical) = value;
        return before != Down();
    }
};
}
