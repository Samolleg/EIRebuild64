#pragma once
#include <atomic>

namespace owned {
// Published by the native host. HUD consumers also read the physical keyboard;
// controller modifiers never modify the desktop or Windows keyboard state.
inline std::atomic<unsigned> controller_modifiers{0};
inline void set_controller_modifiers(unsigned mask) noexcept {
    controller_modifiers.store(mask, std::memory_order_relaxed);
}
inline bool controller_modifier_down(unsigned key) noexcept {
    const unsigned bit = key == 0x11 ? 1u : key == 0x12 ? 2u : key == 0x10 ? 4u : 0u;
    return (controller_modifiers.load(std::memory_order_relaxed) & bit) != 0;
}
}
