#include "E2EffectDistance.h"
#include <atomic>
#include <cmath>

namespace { std::atomic<float> effectDistance{100.0f}; }
namespace OwnedRender {
float E2EffectDistance() noexcept { return effectDistance.load(std::memory_order_relaxed); }
}
namespace owned {
void set_game_effect_distance(float distance) noexcept {
    effectDistance.store(std::isfinite(distance) && distance > 100.0f ? distance : 100.0f,
        std::memory_order_relaxed);
}
}
