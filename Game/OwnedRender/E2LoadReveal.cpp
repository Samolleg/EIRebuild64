#include "E2LoadReveal.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#undef min
#undef max
#include "owned_world_frame.h"
#include "embedded_sky.h"
#include "../GameJournal.h"
#include <mutex>

namespace {
std::mutex requestLock;
bool localMissionActive = false;
std::uint64_t localMissionEpoch = 0;
bool rendererHolds(std::uint64_t epoch) noexcept {
    return epoch && owned::g_load_reveal_enabled.load(std::memory_order_relaxed) &&
        epoch != owned::g_load_reveal_revealed_epoch.load(std::memory_order_relaxed) &&
        owned::embedded_sky().installed();
}
OwnedRender::LoadRevealPause pause;
}
namespace owned {
bool bridge_local_single_player_mission() noexcept {
    if (const auto* snapshot = bridge_embedded_installed_frame()) return snapshot->local_single_player;
    std::lock_guard<std::mutex> lock(requestLock);
    return localMissionActive && localMissionEpoch && localMissionEpoch == bridge_current_world_epoch();
}
void bridge_set_load_reveal(bool value, std::uint64_t epoch) noexcept {
    std::lock_guard<std::mutex> lock(requestLock);
    g_load_reveal_revealed_epoch.store(epoch, std::memory_order_relaxed);
    g_load_reveal_enabled.store(value, std::memory_order_relaxed);
    g_load_reveal_unreported_draws.store(0, std::memory_order_relaxed);
}
}
namespace OwnedRender {
bool E2LoadRevealHeld(std::uint64_t epoch) noexcept {
    std::lock_guard<std::mutex> lock(requestLock);
    return rendererHolds(epoch);
}
void E2LoadRevealFrame(std::uint64_t epoch, bool localMission, bool paused,
                      SetLoadPause set, void* context) noexcept {
    bool hold = false;
    {
        std::lock_guard<std::mutex> lock(requestLock);
        localMissionActive = localMission; localMissionEpoch = epoch;
        const unsigned missed = owned::g_load_reveal_unreported_draws.load(std::memory_order_relaxed);
        if (missed < 30)
            owned::g_load_reveal_unreported_draws.store(missed + 1, std::memory_order_relaxed);
        hold = rendererHolds(epoch) && missed < 30;
    }
    const auto event = pause.Update(owned::bridge_world_draw_completion_counter(), epoch, hold, localMission, paused,
        [&](bool value) { return set && set(context, value); });
    const char* detail = nullptr;
    switch (event) {
    case LoadRevealPause::Event::paused: detail = "paused_for_loading_picture"; break;
    case LoadRevealPause::Event::already_paused: detail = "existing_pause_preserved"; break;
    case LoadRevealPause::Event::resumed: detail = "load_pause_released"; break;
    default: break;
    }
    if (detail) GameJournal::Event("RENDER", "load_pause", "epoch=%llu state=%s", epoch, detail);
}
}
