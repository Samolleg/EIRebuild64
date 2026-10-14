#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// Game figure bounding volume (figure +0x188, supported game.exe): flags
// (1 sphere, 2 box), centre, radius and box minimum/maximum in figure space.
// Stock CFigureClientSpecific::Draw tests exactly this volume
// (CheckVisibility) before it draws a figure, so it is a game-owned selection
// volume that needs no renderer state.
struct FigureBoundingVolume {
    std::uint32_t flags = 0;
    std::array<float, 3> center{};
    float radius = 0;
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

inline bool figure_volume_box(const FigureBoundingVolume& volume,
                              std::array<float, 3>& minimum,
                              std::array<float, 3>& maximum) noexcept {
    const auto finite = [](const std::array<float, 3>& v) {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    };
    if (volume.flags & 2u) {
        if (!finite(volume.minimum) || !finite(volume.maximum)) return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (volume.minimum[axis] > volume.maximum[axis]) return false;
        minimum = volume.minimum;
        maximum = volume.maximum;
        return true;
    }
    if (volume.flags & 1u) {
        if (!finite(volume.center) || !std::isfinite(volume.radius) || volume.radius < 0) return false;
        for (unsigned axis = 0; axis < 3; ++axis) {
            minimum[axis] = volume.center[axis] - volume.radius;
            maximum[axis] = volume.center[axis] + volume.radius;
        }
        return true;
    }
    return false;
}

// Selection box of one figure. The owned shape-selected AABB is used when the
// render sample supplied one; otherwise the game's own volume. Whether the
// renderer can draw the object this frame never removes it from selection.
inline bool figure_selection_box(const std::array<float, 3>* owned_minimum,
                                 const std::array<float, 3>* owned_maximum,
                                 const FigureBoundingVolume& volume,
                                 std::array<float, 3>& minimum,
                                 std::array<float, 3>& maximum) noexcept {
    if (owned_minimum && owned_maximum) {
        minimum = *owned_minimum;
        maximum = *owned_maximum;
        return true;
    }
    return figure_volume_box(volume, minimum, maximum);
}
