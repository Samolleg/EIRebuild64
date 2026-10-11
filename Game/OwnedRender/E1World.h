#pragma once
#include "engine_bridge.h"

namespace OwnedRender {
// Values copied at the world seam. No borrowed world/resource data survives it.
struct E1WorldFrame {
    owned::WorldEnvironment environment{};
    owned::SceneWorldToClipSnapshot camera{};
    owned::WorldFogSnapshot fog{};
};
void E1BeginFrame() noexcept;
void E1RetireWorld(const void* owner) noexcept;
class E1WorldDraw {
    std::uint64_t token = 0;
public:
    E1WorldDraw(const void* owner, std::uint64_t generation,
                const E1WorldFrame& values) noexcept;
    ~E1WorldDraw() noexcept;
    void Complete() noexcept;
    E1WorldDraw(const E1WorldDraw&) = delete;
    E1WorldDraw& operator=(const E1WorldDraw&) = delete;
};
}
