#pragma once

// Cave lighting profile (renderer art direction, not a game rule).
//
// What the game does (supported build): CWorldClient::AdjustLightingWithTime
// (0x00566360) tests CWorldClient+0x6E0 == 1 (GMS_CAVE, the zone's map.txt
// "#sky cave") and then lights the zone from LightsCave<region>.ini at index 0
// every frame: a constant directional light toward (0.5, 0.5, -0.707) with the
// table's [sunlight] colour, the table's [ambient], and the [sky] colour as
// clear and fog colour. The three shipped cave tables are identical: sunlight
// 161,179,170, ambient 75,75,89, sky 0,0,0.
//
// User direction (2026-10-03, revised 2026-10-26, #208): caves are unlit
// underground spaces. There is no directional light and no sun shadow in a
// cave (a sharp terrain shadow across a cave floor reads wrong); the cave is
// lit by its ambient, the ambient occlusion and the local lights (point lights
// with their shadows, lava and other emissive light), which add on top
// unchanged. cave_brightness scales the game's cave ambient (1 is the game's
// table) and cave_ambient_floor keeps the brightest ambient channel at least
// that level so unlit areas stay navigable. The default floor .30 puts an
// unlit cave floor at about 35% of a Gipat noon floor (luma .87), the dark
// end of the 35-55% the 2026-10-03 direction asked for, now that the
// directional term is gone. Outdoor zones are untouched.

#include <algorithm>
#include <array>

namespace owned {

inline constexpr float kDefaultCaveBrightness = 0.7f;
inline constexpr float kDefaultCaveAmbientFloor = 0.30f;

inline void cave_lighting_profile(bool cave, float brightness, float floor, std::array<float, 4>& ambient,
                                  std::array<float, 4>& sun_diffuse) noexcept {
    if (!cave || !(brightness >= 0.0f)) return;
    for (int k = 0; k < 3; ++k) {
        ambient[k] *= brightness;
        sun_diffuse[k] = 0.0f;
    }
    const float brightest = (std::max)({ambient[0], ambient[1], ambient[2]});
    if (floor > 0.0f && brightest < floor) {
        if (brightest > 1e-4f)
            for (int k = 0; k < 3; ++k) ambient[k] *= floor / brightest;
        else
            for (int k = 0; k < 3; ++k) ambient[k] = floor;
    }
}

}  // namespace owned
