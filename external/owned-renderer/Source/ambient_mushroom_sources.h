#pragma once
#include "ambient_particles.h"
#include "owned_asset_loader.h"
#include <vector>

namespace owned::ambient {

// Reuse the source texture's third byte. A 3 m radial falloff at tile centres
// gets a soft fringe from the shader's existing 2 m bilinear sampling. Overlap
// takes the strongest source rather than multiplying a cluster's brightness.
inline void mushroom_sources(const OwnedMapSnapshot* map, std::uint32_t tiles_x, std::uint32_t tiles_y,
    std::vector<std::array<std::uint8_t, 4>>& sources) {
    for (auto& source : sources) source[2] = 0;
    if (!map) return;
    constexpr float radius = 3.f;
    for (const auto& object : map->objects) {
        if (!mushroom_model(object.visual_reference.c_str())) continue;
        const float px = object.position[0], py = object.position[1];
        if (!std::isfinite(px) || !std::isfinite(py) || px < -radius || py < -radius ||
            px > tiles_x*2.f+radius || py > tiles_y*2.f+radius) continue;
        const int x0 = std::max(0, int(std::floor((px-radius)*.5f)));
        const int y0 = std::max(0, int(std::floor((py-radius)*.5f)));
        const int x1 = std::min(int(tiles_x)-1, int(std::floor((px+radius)*.5f)));
        const int y1 = std::min(int(tiles_y)-1, int(std::floor((py+radius)*.5f)));
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const float d = std::hypot(2.f*x+1.f-px, 2.f*y+1.f-py);
            const float t = std::clamp(1.f-d/radius, 0.f, 1.f);
            auto& value = sources[std::size_t(y)*tiles_x+x][2];
            value = std::max(value, static_cast<std::uint8_t>(std::lround(t*t*(3.f-2.f*t)*255.f)));
        }
    }
}

} // namespace owned::ambient
