#pragma once
#include "engine_bridge.h"

namespace OwnedRender {
// Values copied at the world seam. No borrowed world/resource data survives it.
struct E1WorldFrame {
    owned::WorldEnvironment environment{};
    owned::SceneWorldToClipSnapshot camera{};
    owned::WorldFogSnapshot fog{};
    // Completed source light, with direction toward the sun (c56 convention).
    // Validity is explicit: NativeWorldLighting defaults are not source facts.
    owned::NativeWorldLighting lighting{};
    bool lightingValid = false;
};
void E1BeginFrame() noexcept;
// Returns one copied camera/fog/environment/light tuple during the active
// world draw. Invalid subfields remain explicit in the returned snapshot.
bool E1ReadWorldFrame(E1WorldFrame& output) noexcept;
// Copies the current world's light; unavailable after an aborted/retired frame.
bool E1ReadWorldLighting(owned::NativeWorldLighting& output) noexcept;
bool E1ReadWorldEnvironment(owned::WorldEnvironment& output) noexcept;
// At the actual stock sky gate, true transfers the draw to c56.
bool E1ClaimSky(bool requested) noexcept;
std::uint64_t E1WorldGeneration() noexcept;
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
