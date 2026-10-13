#include "NeighbourTerrain.h"
#include <cmath>
#include <mutex>

namespace {
std::mutex placementMutex;
std::array<float, 16> placementValue{};
const void* placementOwner = nullptr;
}
namespace OwnedRender {
void PublishTerrainPlacement(const void* owner, const std::array<float, 16>& matrix) noexcept {
    const std::lock_guard<std::mutex> lock(placementMutex);
    placementOwner = nullptr;
    if (!owner) return;
    for (float value : matrix) if (!std::isfinite(value)) return;
    placementValue = matrix;
    placementOwner = owner;
}
void RetireTerrainPlacement(const void* owner) noexcept {
    const std::lock_guard<std::mutex> lock(placementMutex);
    if (placementOwner == owner) placementOwner = nullptr;
}
}
namespace owned {
bool bridge_neighbour_world_transform(std::array<float,16>& placement) noexcept {
    const std::lock_guard<std::mutex> lock(placementMutex);
    if (!placementOwner) return false;
    placement = placementValue;
    return true;
}

} // namespace owned
