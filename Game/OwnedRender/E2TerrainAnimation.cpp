#include "E2TerrainAnimation.h"
#include "engine_bridge.h"
#include "embedded_sky.h"
#include <atomic>

namespace { std::atomic<bool> requested{false}; }
namespace owned {
void bridge_set_native_terrain_animation_owned(bool enabled) noexcept {
    requested.store(enabled, std::memory_order_relaxed);
}
}
namespace OwnedRender {
bool E2TerrainAnimationOwned() noexcept {
    // Renderer::release revokes the request before releasing its GPU state.
    // A configuration request alone cannot suppress the stock vertex updates.
    return requested.load(std::memory_order_relaxed) && owned::embedded_sky().installed();
}
}
