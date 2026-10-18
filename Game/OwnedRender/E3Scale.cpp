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
float widescreen_combat_text_scale() noexcept { return combatScale; }
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
}
