#include "E1World.h"
#include "embedded_sky.h"
#include <cmath>

namespace {
const void* worldOwner = nullptr;
std::uint64_t worldGeneration = 0, nextEpoch = 0, epoch = 0;
std::uint64_t frame = 0, draw = 0, activeDraw = 0, completions = 0;
OwnedRender::E1WorldFrame values{};
owned::WorldDrawCompletionSnapshot completed{};
std::uint64_t skyRevision = 0;
std::uint64_t advance(std::uint64_t& counter) noexcept {
    if (++counter == 0) ++counter;
    return counter;
}
void clearFrame() noexcept {
    values = {};
    completed = {};
    activeDraw = 0;
    skyRevision = 0;
}
}
namespace OwnedRender {
void E1BeginFrame() noexcept { advance(frame); clearFrame(); }
bool E1ReadWorldLighting(owned::NativeWorldLighting& output) noexcept {
    output = {};
    if (!values.camera.valid || !values.lightingValid) return false;
    output = values.lighting;
    return true;
}
bool E1ClaimSky(bool requested) noexcept {
    const bool claim = requested && activeDraw && values.camera.valid &&
        values.environment.valid && owned::embedded_sky().enabled();
    skyRevision = claim ? owned::embedded_sky().revision() : 0;
    return claim;
}
std::uint64_t E1WorldGeneration() noexcept { return values.camera.valid ? worldGeneration : 0; }
void E1RetireWorld(const void* owner) noexcept {
    if (owner != worldOwner) return;
    worldOwner = nullptr; worldGeneration = epoch = 0;
    clearFrame();
}
E1WorldDraw::E1WorldDraw(const void* owner, std::uint64_t generation,
                        const E1WorldFrame& input) noexcept {
    clearFrame();
    if (!frame || !owner || !generation) return;
    if (owner != worldOwner || generation != worldGeneration) {
        worldOwner = owner; worldGeneration = generation; epoch = advance(nextEpoch);
    }
    token = activeDraw = advance(draw);
    values = input;
    const auto& light = values.lighting;
    for (float v : light.ambient) if (!std::isfinite(v)) values.lightingValid = false;
    for (float v : light.sunlight_diffuse) if (!std::isfinite(v)) values.lightingValid = false;
    for (float v : light.sunlight_direction) if (!std::isfinite(v)) values.lightingValid = false;
    const auto& d = light.sunlight_direction;
    const float lengthSquared = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
        values.lightingValid = false;
    auto& environment = values.environment;
    environment.valid = environment.valid && std::isfinite(environment.game_hours) &&
        std::isfinite(environment.world_time) && std::isfinite(environment.hour) &&
        std::isfinite(environment.weather_progress) && std::isfinite(environment.rain_particles);
    values.camera.scene_frame_serial = frame;
    for (float v : values.camera.world_to_clip)
        if (!std::isfinite(v)) values.camera.valid = false;
    values.fog.scene_frame_serial = frame;
    values.fog.environment = environment;
    if (values.fog.valid) {
        const auto& fog = values.fog.fog;
        if (!std::isfinite(fog.start) || !std::isfinite(fog.end) || !std::isfinite(fog.scale) ||
            !std::isfinite(fog.projection_33) || !std::isfinite(fog.projection_34) ||
            !std::isfinite(fog.projection_43) || !(fog.end > fog.start) || !(fog.scale > 0.f)) {
            values.fog.valid = false;
            values.fog.failure = "host_fog_values_invalid";
        } else if (!environment.valid) {
            values.fog.valid = false;
            values.fog.failure = "host_environment_unavailable";
        } else values.fog.failure = "";
    }
}
E1WorldDraw::~E1WorldDraw() noexcept {
    // An exception or an early return cannot publish a successful world draw.
    if (token && activeDraw == token) clearFrame();
}
void E1WorldDraw::Complete() noexcept {
    if (!token || activeDraw != token) return;
    completed = {frame, advance(completions), true};
    activeDraw = 0;
    token = 0;
}
}
namespace owned {
void bridge_set_owned_sky(bool enabled) noexcept { embedded_sky().configure(enabled); }
bool bridge_owned_sky_installed() noexcept { return embedded_sky().installed(); }
bool bridge_owned_sky_requested() noexcept { return skyRevision && skyRevision == embedded_sky().revision() && embedded_sky().enabled(); }
bool bridge_read_world_environment(WorldEnvironment& output) noexcept {
    output = values.environment;
    return output.valid;
}
bool bridge_current_world_time(double& output) noexcept {
    if (!values.environment.valid) return false;
    output = values.environment.world_time;
    return true;
}
std::uint64_t bridge_current_world_epoch() noexcept { return epoch; }
SceneWorldToClipSnapshot bridge_scene_world_to_clip_snapshot() noexcept { return values.camera; }
WorldFogSnapshot bridge_world_fog_snapshot() noexcept { return values.fog; }
WorldDrawCompletionSnapshot bridge_world_draw_completion_snapshot() noexcept {
    return activeDraw ? WorldDrawCompletionSnapshot{} : completed;
}
std::uint64_t bridge_world_draw_completion_counter() noexcept { return completions; }
}
