#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "E1World.h"
#include "embedded_sky.h"
#include "embedded_host.h"
#include "E1Scene.h"
#include "E1Terrain.h"
#include "character_pane_owned_text.h"
#include "E1VisualFrame.h"
#include "../LiveObserver.h"
#include "precipitation.h"
#include "weather_wind.h"
#include <mutex>
#include <cmath>

namespace {
const void* worldOwner = nullptr;
std::uint64_t worldGeneration = 0, nextEpoch = 0, epoch = 0;
std::uint64_t frame = 0, draw = 0, activeDraw = 0, completions = 0;
OwnedRender::E1WorldFrame values{};
owned::WorldDrawCompletionSnapshot completed{};
std::uint64_t skyRevision = 0;
int weatherTest = 0, weatherStrength = 0;
struct MovieState { std::mutex lock; owned::ProgressMovie value; };
MovieState& movieState() {
    // Game-manager destruction may close a movie after ordinary globals.
    // Its owner explicitly clears the source; the empty registry stays alive.
    static MovieState* value = new MovieState;
    return *value;
}
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
void E1SetProgressMovie(const owned::ProgressMovie& movie) {
    auto& state = movieState();
    std::lock_guard<std::mutex> lock(state.lock);
    state.value = movie;
}
bool E1ReadWorldFrame(E1WorldFrame& output) noexcept {
    output = {};
    if (!activeDraw || !values.camera.valid) return false;
    output = values;
    return true;
}
bool E1ReadWorldLighting(owned::NativeWorldLighting& output) noexcept {
    output = {};
    if (const auto* frame = owned::bridge_embedded_installed_frame()) {
        if (frame->host_lighting_valid) output = frame->host_lighting;
        return frame->host_lighting_valid;
    }
    if (!values.camera.valid || !values.lightingValid) return false;
    output = values.lighting;
    return true;
}
bool E1ReadWorldEnvironment(owned::WorldEnvironment& output) noexcept {
    output = {};
    if (const auto* frame = owned::bridge_embedded_installed_frame()) {
        if (!frame->world_draw_active || !frame->environment.valid) return false;
        output = frame->environment;
        return true;
    }
    if (!activeDraw || !values.camera.valid || !values.environment.valid) return false;
    output = values.environment;
    return true;
}
bool E1ClaimSky(bool requested) noexcept {
    const bool claim = requested && activeDraw && values.camera.valid &&
        values.environment.valid && owned::embedded_sky().enabled();
    skyRevision = claim ? owned::embedded_sky().revision() : 0;
    return claim;
}
std::uint64_t E1WorldGeneration() noexcept {
    if (const auto* frame = owned::bridge_embedded_installed_frame()) return frame->host_world_generation;
    return values.camera.valid ? worldGeneration : 0;
}
void E1RetireWorld(const void* owner) noexcept {
    if (owner != worldOwner) return;
    worldOwner = nullptr; worldGeneration = epoch = 0;
    clearFrame();
    E1OwnedVisualFrameNewDraw();
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
    // Render-only settings affect the same environment consumed by sky,
    // fog, wind and precipitation; the host weather and its sound stay real.
    if (weatherTest == 1 || weatherTest == 2) {
        environment.weather_current = environment.weather_next = weatherTest;
        environment.weather_progress = 1.f;
        environment.rain_particles = weatherTest == 1 ? 1.f : 0.f;
        environment.snow_particles = weatherTest == 2 ? 1.f : 0.f;
    }
    environment.precipitation_strength = owned::precipitation::shower_strength(
        weatherStrength, environment.game_hours, environment.world_time * owned::kWeatherSecondsPerSegment);
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
    LiveObserver::ObserveWorld(values);
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
bool bridge_capture_embedded_frame_values(BridgeFrameValues& output) noexcept try {
    // Host thread at queue submission. No borrowed host pointer survives.
    BridgeFrameValues snapshot;
    snapshot.frame_serial = frame;
    snapshot.world_draw_active = activeDraw != 0;
    snapshot.environment = values.environment;
    snapshot.host_world_generation = OwnedRender::E1WorldGeneration();
    snapshot.host_lighting_valid = OwnedRender::E1ReadWorldLighting(snapshot.host_lighting);
    snapshot.world_time = values.environment.world_time;
    snapshot.world_time_valid = values.environment.valid;
    snapshot.local_single_player = bridge_local_single_player_mission();
    snapshot.owned_sky_requested = bridge_owned_sky_requested();
    snapshot.completion = activeDraw ? WorldDrawCompletionSnapshot{} : completed;
    snapshot.completion_counter = completions;
    snapshot.world_to_clip = values.camera;
    snapshot.fog = values.fog;
    snapshot.world_epoch = OwnedRender::E1Scene().Epoch();
    owned_text_snapshot_epochs(snapshot.character_pane_ui_epoch, snapshot.chat_text_ui_epoch);
    snapshot.terrain_atlases = OwnedRender::E1CaptureTerrainAtlases();
    output = std::move(snapshot);
    return true;
} catch (...) {
    output = {};
    embedded_report_render_failure("embedded_input_missing", "scene_queue_frame_capture");
    return false;
}
const char* bridge_owned_weather_problem() noexcept { return nullptr; }
void bridge_set_weather_visual(int test, int strength) noexcept {
    weatherTest = test; weatherStrength = strength;
}
bool bridge_progress_movie(ProgressMovie& movie) noexcept try {
    auto& state = movieState();
    std::lock_guard<std::mutex> lock(state.lock);
    movie = state.value;
    return movie.source && movie.frames && movie.width && movie.height;
} catch (...) { movie = {}; return false; }
void bridge_set_owned_sky(bool enabled) noexcept { embedded_sky().configure(enabled); }
bool bridge_owned_sky_installed() noexcept { return embedded_sky().installed(); }
bool bridge_owned_sky_requested() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->owned_sky_requested;
    return skyRevision && skyRevision == embedded_sky().revision() && embedded_sky().enabled(); }
bool bridge_read_world_environment(WorldEnvironment& output) noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) output = snapshot->environment;
    else output = values.environment;
    return output.valid;
}
bool bridge_current_world_time(double& output) noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) {
        output = snapshot->world_time; return snapshot->world_time_valid;
    }
    if (!values.environment.valid) return false;
    output = values.environment.world_time;
    return true;
}
// One world epoch for scene and draw, as the game.exe bridge reads it from the
// lifecycle: a separate draw counter made published statics an "earlier world".
std::uint64_t bridge_current_world_epoch() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->world_epoch;
    return OwnedRender::E1Scene().Epoch();
}
SceneWorldToClipSnapshot bridge_scene_world_to_clip_snapshot() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->world_to_clip;
    return values.camera;
}
WorldFogSnapshot bridge_world_fog_snapshot() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->fog;
    return values.fog;
}
WorldDrawCompletionSnapshot bridge_world_draw_completion_snapshot() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->completion;
    return activeDraw ? WorldDrawCompletionSnapshot{} : completed;
}
std::uint64_t bridge_world_draw_completion_counter() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->completion_counter;
    return completions;
}
}
