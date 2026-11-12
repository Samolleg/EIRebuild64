#pragma once
#include <cmath>

namespace Gamepad64 {
// WM_KEY* carries generic modifier VKs, unlike MAPVK_VSC_TO_VK_EX.
inline unsigned MessageKey(unsigned key) noexcept {
    if (key == 0xa0 || key == 0xa1) return 0x10; // Shift
    if (key == 0xa2 || key == 0xa3) return 0x11; // Control
    if (key == 0xa4 || key == 0xa5) return 0x12; // Alt
    return key;
}
inline int ModifierPartner(int scan) noexcept {
    switch (scan) {
    case 42: return 54; case 54: return 42;
    case 29: return 285; case 285: return 29;
    case 56: return 312; case 312: return 56;
    default: return -1;
    }
}
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
