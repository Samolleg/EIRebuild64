#pragma once

#include <cstdint>

namespace owned {

// Shared registration, fallback and player-settings default.
constexpr int kDefaultSunFoliageFilter = 1;

// Owned instance metadata, independent of the wind toggle. Marks a plant part
// whose triangles sample cut texels; the whole-plant sway bit also marks bark.
constexpr std::uint32_t native_foliage_part_flag = 0x100u;

constexpr bool shadow_foliage_opaque(bool enabled, bool point_shadow,
    unsigned slice, std::uint32_t flags, bool alpha_test) noexcept {
    // The existing coverage setup reserves 0-2 for close maps and 3-4 for far.
    return enabled && !point_shadow && slice >= 3 && slice < 5 && alpha_test &&
        (flags & native_foliage_part_flag) != 0;
}

} // namespace owned
