#pragma once
#include <cstddef>
namespace OwnedRender {
float E3DrawRadius(float nativeRadius, unsigned width, unsigned height) noexcept;
void E3CombatVertices(void* vertices, std::size_t count, std::size_t stride) noexcept;
}
