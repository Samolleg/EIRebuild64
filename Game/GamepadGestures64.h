#pragma once
#include <cstdint>

namespace Gamepad64 {
enum class StickClick { none, left, right, both };
struct StickClicks {
    unsigned previous = 0;
    bool chord = false;
    StickClick Update(bool left, bool right) noexcept {
        const unsigned down = (left ? 1u : 0u) | (right ? 2u : 0u);
        if (chord) {
            previous = down;
            if (!down) chord = false;
            return StickClick::none;
        }
        if (down == 3) { chord = true; previous = down; return StickClick::both; }
        const unsigned released = previous & ~down;
        previous = down;
        if (released & 1) return StickClick::left;
        if (released & 2) return StickClick::right;
        return StickClick::none;
    }
};
enum class Press { none, shortPress, longPress };
struct TimedPress {
    bool held = false, fired = false;
    std::uint64_t since = 0;
    Press Update(bool down, std::uint64_t now) noexcept {
        if (!held) {
            if (down) { held = true; fired = false; since = now; }
            return Press::none;
        }
        const bool longEnough = now - since >= 450;
        if (!down) {
            held = false;
            if (fired) return Press::none;
            return longEnough ? Press::longPress : Press::shortPress;
        }
        if (longEnough && !fired) { fired = true; return Press::longPress; }
        return Press::none;
    }
};
enum class PickerEvent { none, advance, commit };
struct TriggerPicker {
    bool bumper = false, trigger = false, consumed = false, selected = false, cancelled = false;
    void Abort() noexcept { cancelled = true; selected = false; }
    PickerEvent Update(bool hold, unsigned value) noexcept {
        const bool down = trigger ? value > 30 : value >= 60;
        const bool advance = hold && down && (!trigger || !bumper);
        const bool commit = bumper && !hold && selected;
        bumper = hold; trigger = down;
        if (value <= 30) consumed = false;
        if (cancelled) {
            if (down) consumed = true;
            if (!hold) cancelled = false;
            return PickerEvent::none;
        }
        if (advance) { consumed = true; selected = true; return PickerEvent::advance; }
        if (!hold) selected = false;
        return commit ? PickerEvent::commit : PickerEvent::none;
    }
    bool BlocksWheel() const noexcept { return bumper || consumed; }
};
inline int StepPose(int pose, int direction) noexcept {
    const int next = pose + direction;
    return next < 0 ? 0 : next > 3 ? 3 : next;
}
inline int NextSlot(int current, int count, int direction = 1) noexcept {
    if (count <= 0) return -1;
    if (current < 0 || current >= count) return direction > 0 ? 0 : count - 1;
    return (current + count + direction) % count;
}
}
