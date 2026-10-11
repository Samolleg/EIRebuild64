#include "engine_bridge.h"
#include "test_harness.h"
#include "res_cache_release.h"
#include "../GameJournal.h"
#include <atomic>

namespace {
void unavailable(const char* feature, std::atomic_flag& once) noexcept {
    if (!once.test_and_set())
        GameJournal::Event("RENDER", "unavailable", "%s: unavailable in embedded build; retail instrumentation is not installed", feature);
}
#define E2_UNAVAILABLE(feature) do { static std::atomic_flag once = ATOMIC_FLAG_INIT; unavailable(feature, once); } while (false)
}
namespace owned {
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
    GameJournal::Event("RENDER", "bridge", "event=%.96s detail=%.768s", event ? event : "", detail ? detail : "");
}
void bridge_append_probe_event(const char*, const char*) noexcept { E2_UNAVAILABLE("retail lifecycle probe"); }
void bridge_trace_input_focus(HWND, const char*, UINT, WPARAM, int) noexcept { E2_UNAVAILABLE("retail input focus trace"); }
bool bridge_game_sites_all_verified() noexcept { E2_UNAVAILABLE("retail game site verification"); return false; }
bool bridge_semantic_slots_installed() noexcept { E2_UNAVAILABLE("retail semantic hooks (direct frame/UI scopes only)"); return false; }
const BridgeProfileSnapshot* bridge_latest_profile() noexcept { return nullptr; }
void bridge_set_profile_enabled(bool enabled) { if (enabled) { E2_UNAVAILABLE("retail bridge profile"); } }
void bridge_report_hovered_shadow() noexcept { E2_UNAVAILABLE("retail hovered-shadow capture"); }
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
