#pragma once
#include <array>
#include <cstdint>

namespace owned { class Renderer; struct FixedState; }

namespace OwnedRender {
// Host values of one world draw, without renderer headers (WorldClient).
struct E1VisualInputs {
    double time = 0;                        // segment ticks + fraction
    std::uint32_t sceneFlags = 0;           // CWorldClient flags (WC_RENDER_*)
    bool drawUndetected = true;
    std::array<float, 16> worldToClip{};
    std::array<float, 4> ambient{}, sunDiffuse{}, sunDirection{};
    bool lightingValid = false;
    unsigned char (*fog)(void* context, const float* position) = nullptr;
    void* fogContext = nullptr;
};
// Runs the renderer's owned-visual world frame for this draw (sun and point
// shadows, point lights, colour of every visual the retained scene claims,
// and the static set flush) with native sinks bound directly to the embedded
// renderer. Called inside the renderer facade's lock. Returns false when the
// frame was rejected.
bool E1RunOwnedVisualFrame(owned::Renderer& renderer, const owned::FixedState& state,
                           const E1VisualInputs& inputs) noexcept;
// Whether the static set was flushed by the last E1RunOwnedVisualFrame.
bool E1OwnedVisualFrameFlushedStatics() noexcept;
// Whether the last owned-visual frame drew this object. Its legacy figures
// and stock shadow are then skipped (game.exe bridge: claimed visuals); an
// object the frame did not draw keeps its legacy path, so nothing vanishes.
bool E1OwnedVisualDrawn(std::int32_t id) noexcept;
// Called at each world draw start: forgets the drawn set when the previous
// world draw ran no owned-visual frame.
void E1OwnedVisualFrameNewDraw() noexcept;
}
