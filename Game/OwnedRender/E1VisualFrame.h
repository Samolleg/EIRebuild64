#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class CObject3DClientSpecific;
namespace owned { class Renderer; struct FixedState; struct OwnedVisualSurface; struct RetainedVisualFrame; struct VisualResourceIncarnation; }

namespace owned { struct RetainedFireSpell; }
namespace OwnedRender {
// Typed local-server facts sampled on the game thread between simulation ticks.
void E1ReadVisualFireSpells(std::vector<owned::RetainedFireSpell>& output);
// Publish live source appearance before retained frame sampling.
bool E1CompleteVisualAppearance(CObject3DClientSpecific& specific,
    owned::VisualResourceIncarnation resource) noexcept;
bool E1CompleteVisualFrame(owned::RetainedVisualFrame& frame) noexcept;
// The same completed unit facts for cover/contacts, without HUD publication.
bool E1CompleteAmbientFrame(owned::RetainedVisualFrame& frame) noexcept;
// Host values of one world draw, without renderer headers (WorldClient).
struct E1VisualInputs {
    double time = 0;                        // segment ticks + fraction
    std::uint32_t sceneFlags = 0;           // CWorldClient flags (WC_RENDER_*)
    bool drawUndetected = true;
    bool briefingScreen = false;
    std::int32_t hoverId = 0;
    std::uint64_t hoverSerial = 0;
    std::array<float, 16> worldToClip{};
    float cameraAnchorZ = 0; // CCamera anchor height for the close shadow band
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
// Whether this draw's owned frame accepted an object's colour.
// Unclaimed objects are skipped, with no legacy colour/shadow fallback.
bool E1OwnedVisualDrawn(std::int32_t id) noexcept;
// First omission reason from the last owned frame, or null if that frame
// contains the visual and accepted its colour.
const char* E1OwnedVisualOmission(std::int32_t id) noexcept;
// Reports acceptance/rejection only when an object's state changes.
void E1ReportOwnedVisual(std::int32_t id) noexcept;
// Records a rejected owned pose for the once-per-epoch step summary.
void E1ReportOwnedPoseFailure(std::int32_t id, const char* step) noexcept;
// Each world draw unconditionally retires the previous result.
void E1OwnedVisualFrameNewDraw() noexcept;
void E1PublishSecondaryVisual(std::int32_t id, std::uint64_t primary,
    std::shared_ptr<const owned::OwnedVisualSurface> secondary) noexcept;
}
