#include "E1World.h"
#include <cmath>

namespace {
const void* worldOwner = nullptr;
std::uint64_t worldGeneration = 0, nextEpoch = 0, epoch = 0;
std::uint64_t frame = 0, draw = 0, activeDraw = 0, completions = 0;
OwnedRender::E1WorldFrame values{};
owned::WorldDrawCompletionSnapshot completed{};
std::uint64_t advance(std::uint64_t& counter) noexcept {
    if (++counter == 0) ++counter;
    return counter;
}
void clearFrame() noexcept {
    values = {};
    completed = {};
    activeDraw = 0;
}
}
namespace OwnedRender {
void E1BeginFrame() noexcept { advance(frame); clearFrame(); }
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
    auto& environment = values.environment;
    environment.valid = environment.valid && std::isfinite(environment.game_hours) &&
        std::isfinite(environment.world_time) && std::isfinite(environment.hour) &&
        std::isfinite(environment.weather_progress) && std::isfinite(environment.rain_particles);
    values.camera.scene_frame_serial = frame;
    for (float v : values.camera.world_to_clip)
        if (!std::isfinite(v)) values.camera.valid = false;
    values.fog.scene_frame_serial = frame;
    values.fog.environment = environment;
    values.fog.valid = values.fog.valid && environment.valid;
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
