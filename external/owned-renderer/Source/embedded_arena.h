#pragma once
#include <array>
#include <cstdint>

namespace owned {
// Synchronous values only. Actor pointers, script variables and game state
// stay with the host. Clocks are the same 18-tick clock used by arena damage.
struct EmbeddedArenaTower {
    std::array<float,3> crystal{};
    bool present = false, broken = false;
    float break_time = 0;
};
struct EmbeddedArenaInput {
    std::uint64_t world_epoch = 0;
    std::int32_t boss_id = 0, tick = 0;
    std::array<float,3> base{}, camera{};
    std::array<float,16> world_to_clip{};
    bool dome = false, flow = false;
    float dome_time = 0, flow_time = 0, flow_fraction = 0;
    int phase = 0, wave = 0;
    float wave_radius = 0;
    std::array<EmbeddedArenaTower,3> towers{};
};
struct EmbeddedArenaCache {
    std::uint64_t serial = 0;
    // Stable renderer-owned slot identities, never temporary vector addresses.
    std::array<std::uint32_t,32> slots{};
};
} // namespace owned
