#include "engine_bridge.h"
#include "test_harness.h"
#include "res_cache_release.h"
#include "../GameJournal.h"
#include "../CorePerf.h"
#include <atomic>
#include <array>
#include <cstring>
#include <cstdio>
#include <mutex>
#include "embedded_host.h"

namespace {
void unavailable(const char* feature, std::atomic_flag& once) noexcept {
    if (!once.test_and_set())
        GameJournal::Event("RENDER", "unavailable", "%s: unavailable in embedded build; retail instrumentation is not installed", feature);
}
#define E2_UNAVAILABLE(feature) do { static std::atomic_flag once = ATOMIC_FLAG_INIT; unavailable(feature, once); } while (false)
}
namespace owned {
void embedded_report_render_failure(const char* event, const char* reason) noexcept try {
    // The first occurrence of each cause in each world, then one reminder
    // every 600 failures. Fixed storage bounds both memory and log traffic.
    struct Failure { char event[64]{}; char reason[192]{}; std::uint64_t count = 0; };
    static std::mutex guard;
    static std::array<Failure, 32> failures{};
    static std::uint64_t lastEpoch = 0;
    const auto epoch = bridge_current_world_epoch();
    const auto* name = event ? event : "render_failure";
    const auto* cause = reason ? reason : "unknown";
    std::lock_guard<std::mutex> lock(guard);
    if (lastEpoch != epoch) { failures = {}; lastEpoch = epoch; }
    for (auto& failure : failures) {
        if (!failure.event[0]) {
            std::snprintf(failure.event, sizeof(failure.event), "%s", name);
            std::snprintf(failure.reason, sizeof(failure.reason), "%s", cause);
        }
        if (std::strncmp(failure.event, name, sizeof(failure.event) - 1) ||
            std::strncmp(failure.reason, cause, sizeof(failure.reason) - 1)) continue;
        ++failure.count;
        if (failure.count == 1 || failure.count % 600 == 0)
            GameJournal::Event("ERROR", name, "reason=%s epoch=%llu failures=%llu",
                cause, static_cast<unsigned long long>(epoch),
                static_cast<unsigned long long>(failure.count));
        return;
    }
} catch (...) {}

unsigned embedded_frame_scope_begin(const char* name, LONGLONG now) noexcept {
    return CorePerf::BeginFrameDetail(name, now);
}
void embedded_frame_scope_end(unsigned token, LONGLONG now) noexcept {
    CorePerf::EndFrameDetail(token, now);
}

void embedded_perf06_report(const char* detail) noexcept {
    GameJournal::Event("RENDER", "pipeline_perf", "%s", detail);
}

const char* figure_fallback_reason_name(FigureFallbackReason reason) noexcept {
    static constexpr const char* names[] = {"none", "unavailable", "hidden", "renderer state",
        "transform", "material", "light array", "morph data", "geometry", "metadata",
        "state write", "direct submit", "exception"};
    static_assert(sizeof(names) / sizeof(*names) == kFigureFallbackReasonCount);
    const auto index = static_cast<std::size_t>(reason);
    return index < kFigureFallbackReasonCount ? names[index] : "unknown";
}
bool harness_timed_out(std::uint64_t start, std::uint64_t now, std::uint32_t seconds) noexcept {
    return now >= start && now - start >= std::uint64_t(seconds) * 1000;
}
void bridge_report_event(const char* event, const char* detail) noexcept {
    // Bounded strings and the existing bounded asynchronous game journal.
    const bool failure = event && (!std::strcmp(event, "render_thread_command_failed") ||
        !std::strcmp(event, "present_step_failed") || !std::strcmp(event, "composite_step_failed"));
    GameJournal::Event(failure ? "ERROR" : "RENDER", "bridge",
        "event=%.96s detail=%.768s", event ? event : "", detail ? detail : "");
}
void bridge_append_probe_event(const char* event, const char* detail) noexcept {
    // Shared renderer events include terrain/material failures, not just retail probes.
    bridge_report_event(event, detail);
}
void bridge_trace_input_focus(HWND, const char*, UINT, WPARAM, int) noexcept { E2_UNAVAILABLE("retail input focus trace"); }
// The embedded EIRebuild host publishes its own render inputs directly. Retail
// game.exe patch-site and semantic-hook checks do not apply to this frontend.
bool bridge_game_sites_all_verified() noexcept { return true; }
bool bridge_semantic_slots_installed() noexcept { return true; }
const BridgeProfileSnapshot* bridge_latest_profile() noexcept { return nullptr; }
void bridge_set_profile_enabled(bool enabled) { if (enabled) { E2_UNAVAILABLE("retail bridge profile"); } }
void bridge_request_hovered_shadow_capture() noexcept { E2_UNAVAILABLE("retail hovered-shadow capture"); }
void bridge_post_engine_surface_material_group_scan() noexcept {
    E2_UNAVAILABLE("retail post-engine surface material-group timing");
}
bool bridge_outer_frame_timing_supported() noexcept { return false; }
void bridge_set_outer_frame_timing_active(bool active) noexcept { if (active) { E2_UNAVAILABLE("retail outer frame timing"); } }
bool bridge_take_outer_frame_timing(std::int64_t, OuterFrameTimingSnapshot& out) noexcept { out = {}; return false; }
bool bridge_outer_frame_timing_matches(const OuterFrameTimingSnapshot&, const BridgeProfileSnapshot*) noexcept { return false; }
SceneRendererTimingToken bridge_scene_renderer_timing_begin(SceneRendererTimingKind kind) noexcept {
    SceneRendererTimingToken token{}; token.kind = kind; return token;
}
void bridge_scene_renderer_timing_end(SceneRendererTimingToken& token) noexcept { token = {}; }
ShadowInputDiagnostics bridge_shadow_reuse_diagnostics() noexcept {
    E2_UNAVAILABLE("retail shadow reuse observations"); return {};
}
bool harness_enabled() noexcept { E2_UNAVAILABLE("file-driven retail harness"); return false; }
const std::string& harness_directory() noexcept { static const std::string empty; return empty; }
unsigned harness_camera_path_steps() noexcept { return 0; }
const HarnessCommand* harness_present_step() noexcept { return nullptr; }
void harness_complete(bool, const std::string&) noexcept { E2_UNAVAILABLE("retail harness completion"); }
void harness_complete_entry(unsigned long, bool, const std::string&) noexcept { E2_UNAVAILABLE("retail harness completion"); }
void harness_world_frame(void*) noexcept { E2_UNAVAILABLE("retail harness world commands"); }
bool harness_on_travel_map() noexcept { E2_UNAVAILABLE("retail harness travel state"); return false; }
void request_debug_lightning() noexcept { E2_UNAVAILABLE("debug lightning command"); }
void window_sampler_present() { E2_UNAVAILABLE("temporary retail window sampler"); }
ResCacheReleaseSnapshot res_cache_release_snapshot() noexcept {
    // EIRebuild owns resource lifetimes directly; no addon cache hook exists.
    ResCacheReleaseSnapshot result{};
    result.status = ResCacheReleaseStatus::addon_missing;
    return result;
}
}
