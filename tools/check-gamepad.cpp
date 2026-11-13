#include "../Game/GamepadState64.h"
#include "../Game/GamepadGestures64.h"
#include "../Game/GamepadWheel64.h"
#include "../external/owned-renderer/Source/controller_input_state.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

void require(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
int main() {
    using namespace Gamepad64;
    TriggerWheel smooth;
    const int first = smooth.Advance(1, 1.0 / 120, true);
    require(first > 0 && first < 120, "mission zoom moves before a whole wheel detent");
    const auto wheelSum = [](double strength, int hz) {
        TriggerWheel wheel;
        int total = 0;
        for (int n = 0; n < hz; ++n) {
            const int delta = wheel.Advance(strength, 1.0 / hz, true);
            require(delta > 0 && delta < 120, "continuous zoom on every sample");
            total += delta;
        }
        return total;
    };
    for (int hz : {30, 60, 120}) {
        require(std::abs(wheelSum(1, hz) - 720) <= 1, "zoom speed independent of polling rate");
        require(std::abs(wheelSum(.5, hz) - 360) <= 1, "half trigger gives half zoom speed");
    }
    TriggerWheel partial;
    partial.Advance(.1, .008, true);
    require(partial.Advance(0, .008, true) == 0 && partial.remainder == 0, "release clears fractional zoom");
    partial.Advance(.1, .008, true);
    partial.Reset();
    require(partial.Advance(.1, .008, true) == 0, "focus/disconnect reset does not carry fractional zoom");
    require(partial.Advance(-1, .008, true) < 0, "reversal immediately changes zoom direction");
    TriggerWheel menu;
    require(menu.Advance(1, .008, false) == 0, "menu retains discrete wheel events");
    for (int i = 0; i < 60; ++i) require(menu.Advance(1, 1.0/60, false) % 120 == 0, "menu events remain whole detents");
    TriggerWheel context;
    context.Advance(1, .05, false);
    context.Advance(1, .05, false);
    require(context.Advance(1, .008, true) <= 6, "UI remainder cannot burst into camera zoom");
    require(context.Advance(1, 5, true) <= 36, "long stalls cannot cause a catch-up jump");
    TriggerPicker zoomPicker;
    zoomPicker.Update(true, 255);
    TriggerWheel blockedZoom;
    require(blockedZoom.Advance(zoomPicker.BlocksWheel() ? 0 : 1, .016, true) == 0, "picker blocks smooth zoom");
    zoomPicker.Update(false, 255);
    require(blockedZoom.Advance(zoomPicker.BlocksWheel() ? 0 : 1, .016, true) == 0, "bumper release does not leak held trigger");
    zoomPicker.Update(false, 0);
    require(blockedZoom.Advance(1, .016, true) > 0, "fresh trigger resumes smooth zoom");
    owned::set_controller_modifiers(6);
    require(owned::controller_modifier_down(0x10) && owned::controller_modifier_down(0x12) &&
        !owned::controller_modifier_down(0x11), "HUD receives Shift/Alt without physical key injection");
    owned::set_controller_modifiers(0);
    require(!owned::controller_modifier_down(0x10) && !owned::controller_modifier_down(0x12), "HUD modifiers reset");
    require(MessageKey(0xa0) == 0x10 && MessageKey(0xa1) == 0x10, "Shift releases use generic VK");
    require(MessageKey(0xa4) == 0x12 && MessageKey(0xa5) == 0x12, "Alt releases use generic VK");
    require(MessageKey('N') == 'N', "ordinary virtual keys preserved");
    require(ModifierPartner(42) == 54 && ModifierPartner(56) == 312 && ModifierPartner(1) == -1,
        "opposite-side physical modifiers share ownership");
    TimedPress tap;
    require(tap.Update(true, 100) == Press::none, "short action deferred");
    require(tap.Update(false, 200) == Press::shortPress, "short action on release");
    require(tap.Update(false, 300) == Press::none, "short action fires once");
    TimedPress hold;
    require(hold.Update(true, 100) == Press::none, "hold begins without short action");
    require(hold.Update(true, 549) == Press::none, "hold below threshold");
    require(hold.Update(true, 550) == Press::longPress, "long action at threshold");
    require(hold.Update(true, 2000) == Press::none, "long action no autorepeat");
    require(hold.Update(false, 2100) == Press::none, "long release never fires short action");
    TimedPress slowFrame;
    slowFrame.Update(true, 0);
    require(slowFrame.Update(false, 600) == Press::longPress, "delayed poll keeps long semantics");
    TriggerPicker picker;
    require(picker.Update(true, 0) == PickerEvent::none, "bumper alone does not select");
    require(picker.Update(true, 60) == PickerEvent::advance, "trigger edge advances");
    require(picker.Update(true, 255) == PickerEvent::none, "held trigger no repeat");
    require(picker.Update(true, 35) == PickerEvent::none, "trigger hysteresis");
    require(picker.Update(true, 30) == PickerEvent::none, "trigger release does not commit");
    require(picker.Update(true, 60) == PickerEvent::advance, "second trigger press advances");
    require(picker.Update(false, 255) == PickerEvent::commit, "bumper release commits once");
    require(picker.BlocksWheel(), "consumed trigger cannot zoom on bumper release");
    require(picker.Update(false, 255) == PickerEvent::none, "commit not repeated");
    picker.Update(false, 0);
    require(!picker.BlocksWheel(), "trigger neutral restores wheel");
    TriggerPicker cancelled;
    cancelled.Update(true, 100);
    cancelled.Abort();
    require(cancelled.Update(true, 0) == PickerEvent::none, "cancelled selection stays cancelled");
    require(cancelled.Update(true, 100) == PickerEvent::none, "cancel cannot restart until bumper release");
    require(cancelled.Update(false, 100) == PickerEvent::none, "cancel cannot activate on release");
    require(cancelled.BlocksWheel(), "cancelled trigger does not leak to wheel");
    TriggerPicker noSelection;
    noSelection.Update(true, 0);
    require(noSelection.Update(false, 0) == PickerEvent::none, "bumper without trigger never activates");
    for (int pose = 0; pose < 4; ++pose) {
        require(StepPose(pose, 1) == (pose == 3 ? 3 : pose + 1), "pose up clamps at run");
        require(StepPose(pose, -1) == (pose == 0 ? 0 : pose - 1), "pose down clamps at prone");
    }
    require(NextSlot(-1, 4) == 0 && NextSlot(3, 4) == 0 && NextSlot(0, 4, -1) == 3 && NextSlot(0, 0) == -1,
        "cyclic slots, no selection and empty panel");
    MovieButtonGate movie;
    require(!movie.Update(true, 0x1000, true), "held on movie entry cannot skip");
    require(!movie.Update(true, 0x1000, true), "held button remains blocked");
    require(!movie.Update(true, 0, true), "release arms movie input");
    require(movie.Update(true, 0x1000, true), "fresh A skips movie");
    require(!movie.Update(true, 0x1000, true), "hold does not repeat");
    require(!movie.Update(false, 0, true), "focus loss/disconnection disarms");
    require(!movie.Update(true, 0x0010, true), "held Start on reconnect cannot skip");
    require(!movie.Update(true, 0, false), "release during unskippable movie");
    require(!movie.Update(true, 0x0010, false), "unskippable movie consumes press");
    require(!movie.Update(true, 0x0010, true), "becoming skippable cannot reuse held press");
    require(!movie.Update(true, 0, true), "release before next press");
    require(movie.Update(true, 0x0010, true), "fresh Start skips held frame");
    MovieButtonGate nextMovie;
    require(!nextMovie.Update(true, 0x0010, true), "press cannot skip next movie");
    for (unsigned button = 1; button <= 0x8000; button <<= 1) {
        MovieButtonGate gate;
        require(!gate.Update(true, 0, true), "neutral movie entry");
        require(gate.Update(true, button, true), "digital button skip");
    }
    const int dead = 7849;
    require(NormalizeStick(0, 0, dead).x == 0, "center");
    require(NormalizeStick(dead, 0, dead).x == 0, "deadzone boundary");
    require(NormalizeStick(dead + 1, 0, dead).x > 0, "continuous deadzone exit");
    require(NormalizeStick(32767, 0, dead).x == 1, "positive endpoint");
    require(NormalizeStick(-32768, 0, dead).x == -1, "negative endpoint clamped");
    for (int x = -32768; x <= 32767; x += 257) {
        for (int y = -32768; y <= 32767; y += 257) {
            const auto value = NormalizeStick(short(x), short(y), dead);
            require(std::isfinite(value.x) && std::isfinite(value.y), "finite stick");
            require(value.x * value.x + value.y * value.y <= 1.00000001, "radial speed bound");
        }
    }
    require(Trigger(0) == 0 && Trigger(30) == 0, "trigger threshold");
    require(Trigger(31) > 0 && Trigger(255) == 1, "trigger range");
    for (int physical = 0; physical < 2; ++physical)
        for (int pad = 0; pad < 2; ++pad)
            for (int source = 0; source < 2; ++source)
                for (int down = 0; down < 2; ++down) {
                    Button button{bool(physical), bool(pad)};
                    const bool before = physical || pad;
                    const bool after = source ? (physical || down) : (pad || down);
                    require(button.Set(source, down) == (before != after), "ownership edge");
                    require(button.Down() == after, "ownership state");
                }
    Button held;
    require(held.Set(true, true), "pad press");
    require(!held.Set(false, true), "physical takes shared ownership");
    require(!held.Set(true, false) && held.Down(), "pad release preserves physical hold");
    require(held.Set(false, false) && !held.Down(), "last owner releases");
    std::puts("PASS: smooth trigger zoom, gestures, deferred picker, modifiers, stance/cycles, movie skip, sticks, dual-source buttons");
}
