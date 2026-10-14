#pragma once

// Cave lighting profile (renderer art direction, not a game rule).
//
// What the game does (supported build): CWorldClient::AdjustLightingWithTime
// (0x00566360) tests CWorldClient+0x6E0 == 1 (GMS_CAVE) and then lights the
// zone from LightsCave<region>.ini at index 0 every frame: a constant
// directional light toward (0.5, 0.5, -0.707) with the table's [sunlight]
// colour, the table's [ambient], and the [sky] colour as clear and fog colour.
// The three shipped cave tables are identical: sunlight 161,179,170, ambient
// 75,75,89, sky 0,0,0. With the terrain's max(ambient, light*facing) that is
// about 56% of a Gipat noon floor (luma .48 against .87) and 57% in shadow.
//
// User direction (2026-10-03): caves read darker than outdoor daylight, still
// navigable, leaving headroom for lava, lamps and other local light, which add
// on top unchanged. The game's cave light keeps its colour and fixed direction
// (and its shadows); cave_brightness scales the ambient and that light
// together, 1 being the game's own table. Outdoor zones are untouched.

#include <array>

namespace owned {

inline constexpr float kDefaultCaveBrightness = 0.7f;

inline void cave_lighting_profile(bool cave, float brightness, std::array<float, 4>& ambient,
                                  std::array<float, 4>& sun_diffuse) noexcept {
    if (!cave || !(brightness >= 0.0f)) return;
    for (int k = 0; k < 3; ++k) {
        ambient[k] *= brightness;
        sun_diffuse[k] *= brightness;
    }
}

}  // namespace owned
