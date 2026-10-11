#include "E2Host.h"
#include "E2EffectDistance.h"
#include "engine_bridge.h"
#include "game_focus.h"
#include "test_harness.h"
#include "res_cache_release.h"
#include "../GameJournal.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2; // explicit private output directory
    GameJournal::TestStart(argv[1]);
    CHECK(owned::current_semantic_context().kind == owned::SemanticKind::unknown);
    try {
        OwnedRender::E2PassScope frame(OwnedRender::Pass::frame);
        CHECK(owned::current_semantic_context().kind == owned::SemanticKind::frame);
        {
            OwnedRender::E2PassScope ui(OwnedRender::Pass::ui);
            CHECK(owned::current_semantic_context().kind == owned::SemanticKind::ui);
            CHECK(owned::current_semantic_context().caller_rva == 0);
        }
        CHECK(owned::current_semantic_context().kind == owned::SemanticKind::frame);
        throw std::runtime_error("scope unwind");
    } catch (const std::runtime_error&) {}
    CHECK(owned::current_semantic_context().kind == owned::SemanticKind::unknown);
    CHECK(OwnedRender::E2Clock(100, false) == 100);
    CHECK(OwnedRender::E2Clock(110, true) == 110);
    CHECK(OwnedRender::E2Clock(800, true) == 110);
    CHECK(OwnedRender::E2Clock(900, false) == 110);
    CHECK(OwnedRender::E2Clock(910, false) == 120);
    CHECK(!OwnedRender::E2BackgroundConfigured());
    owned::set_background_mode(1);
    CHECK(OwnedRender::E2BackgroundConfigured());
    CHECK(!OwnedRender::E2BackgroundPaused(nullptr));
    owned::set_background_mode(0);
    CHECK(!OwnedRender::E2BackgroundPaused(GetDesktopWindow()));
    owned::set_game_effect_distance(250);
    CHECK(OwnedRender::E2EffectDistance() == 250);
    owned::set_game_effect_distance(20);
    CHECK(OwnedRender::E2EffectDistance() == 100);
    owned::set_game_effect_distance(std::numeric_limits<float>::quiet_NaN());
    CHECK(OwnedRender::E2EffectDistance() == 100);
    owned::set_game_effect_distance(std::numeric_limits<float>::infinity());
    CHECK(OwnedRender::E2EffectDistance() == 100);
    CHECK(!owned::harness_enabled());
    CHECK(owned::harness_directory().empty());
    CHECK(!owned::harness_present_step());
    CHECK(owned::harness_camera_path_steps() == 0);
    CHECK(!owned::harness_on_travel_map());
    CHECK(!owned::harness_timed_out(1000, 999, 1));
    CHECK(!owned::harness_timed_out(1000, 1999, 1));
    CHECK(owned::harness_timed_out(1000, 2000, 1));
    CHECK(std::string(owned::figure_fallback_reason_name(owned::FigureFallbackReason::direct_submit)) == "direct submit");
    CHECK(std::string(owned::figure_fallback_reason_name(owned::FigureFallbackReason::count)) == "unknown");
    CHECK(!owned::bridge_latest_profile());
    CHECK(!owned::bridge_outer_frame_timing_supported());
    owned::OuterFrameTimingSnapshot timing{};
    timing.fresh = true; timing.bridge_frame = 99;
    CHECK(!owned::bridge_take_outer_frame_timing(1000, timing));
    CHECK(!timing.fresh && timing.bridge_frame == 0);
    CHECK(!owned::bridge_outer_frame_timing_matches(timing, nullptr));
    auto token = owned::bridge_scene_renderer_timing_begin(owned::SceneRendererTimingKind::ui_flush);
    CHECK(!token.active && token.begin == 0);
    owned::bridge_scene_renderer_timing_end(token);
    CHECK(!token.active);
    const auto cache = owned::res_cache_release_snapshot();
    CHECK(cache.status == owned::ResCacheReleaseStatus::addon_missing && !cache.hooks_active);
    owned::bridge_report_event("E2Check", "real journal sink; no game, GPU or window created");
    GameJournal::TestDrain();
    GameJournal::Stop();
    std::puts("E2_CHECK_PASS: scopes/unwind, pause clock, settings, unavailable diagnostics, journal");
    return 0;
}
