#pragma once
#include <array>

namespace OwnedRender {
// Values copied from CTerrain's prepared placement at the draw boundary.
// owner is only a lifecycle key, never dereferenced or serialized.
void PublishTerrainPlacement(const void* owner, const std::array<float, 16>& matrix) noexcept;
void RetireTerrainPlacement(const void* owner) noexcept;
}
