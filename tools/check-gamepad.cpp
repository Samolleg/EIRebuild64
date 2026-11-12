#include "../Game/GamepadState64.h"
#include <cstdio>
#include <cstdlib>

void require(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
int main() {
    using namespace Gamepad64;
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
    std::puts("PASS: stick grid, deadzones, trigger range, exhaustive dual-source button transitions");
}
