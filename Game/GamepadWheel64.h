#pragma once

namespace Gamepad64 {
// Mission camera/minimap/text handlers accept wheel units smaller than a
// detent. Other screens retain their existing whole-detent scroll contract.
struct TriggerWheel {
    static constexpr int unitsPerDetent = 120;
    double remainder = 0, previousInput = 0;
    bool continuous = false;
    void Reset() noexcept { remainder = previousInput = 0; }
    int Advance(double input, double seconds, bool mission) noexcept {
        if (!input || seconds <= 0) { Reset(); return 0; }
        if (continuous != mission || input * previousInput < 0) remainder = 0;
        continuous = mission; previousInput = input;
        // Same maximum zoom rate as before, but no six-times-per-second steps.
        remainder += input * 6.0 * (seconds > .05 ? .05 : seconds) * unitsPerDetent;
        const int quantum = mission ? 1 : unitsPerDetent;
        const int delta = int(remainder / quantum) * quantum;
        remainder -= delta;
        return delta;
    }
};
}
