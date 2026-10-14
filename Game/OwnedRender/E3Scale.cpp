#include "E3Scale.h"
#include "widescreen.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>

namespace {
float combatScale = 1.f;
// Preserve the host's existing profile until the renderer supplies its setting.
float landscapeRadius = 0.f;
}
namespace owned {
void widescreen_set_combat_text_scale(int percent) noexcept {
    combatScale = std::clamp(percent, 50, 300) / 100.f;
}
void widescreen_set_landscape_radius(float distance) noexcept {
    if (std::isfinite(distance))
        landscapeRadius = std::clamp(distance, 100.f, 1000.f);
}
}
namespace OwnedRender {
float E3DrawRadius(float nativeRadius, unsigned width, unsigned height) noexcept {
    if (!landscapeRadius) return nativeRadius;
    return landscapeRadius * owned::widescreen::draw_square_factor(float(width), float(height));
}
void E3CombatVertices(void* vertices, std::size_t count, std::size_t stride) noexcept {
    if (!vertices || !count || stride < 2 * sizeof(float) || combatScale == 1.f) return;
    auto* bytes = static_cast<unsigned char*>(vertices);
    float left = (std::numeric_limits<float>::max)(), top = left;
    float right = -left, bottom = -left;
    for (std::size_t i = 0; i < count; ++i) {
        float xy[2]; std::memcpy(xy, bytes + i * stride, sizeof(xy));
        left = (std::min)(left, xy[0]); right = (std::max)(right, xy[0]);
        top = (std::min)(top, xy[1]); bottom = (std::max)(bottom, xy[1]);
    }
    const float x = (left + right) * .5f, y = (top + bottom) * .5f;
    for (std::size_t i = 0; i < count; ++i) {
        float xy[2]; std::memcpy(xy, bytes + i * stride, sizeof(xy));
        xy[0] = x + (xy[0] - x) * combatScale;
        xy[1] = y + (xy[1] - y) * combatScale;
        std::memcpy(bytes + i * stride, xy, sizeof(xy));
    }
}
}
