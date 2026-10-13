#include "E2LoadReveal.h"
#include "engine_bridge.h"
#include "embedded_sky.h"
#include "../GameJournal.h"
#include <mutex>

namespace {
std::mutex requestLock;
bool enabled = false;
std::uint64_t revealed = 0;
OwnedRender::LoadRevealPause pause;
}
namespace owned {
void bridge_set_load_reveal(bool value, std::uint64_t epoch) noexcept {
    std::lock_guard<std::mutex> lock(requestLock);
    enabled = value; revealed = epoch;
}
}
namespace OwnedRender {
void E2LoadRevealFrame(std::uint64_t epoch, bool localMission, bool paused,
                      SetLoadPause set, void* context) noexcept {
    bool hold = false;
    {
        std::lock_guard<std::mutex> lock(requestLock);
        hold = enabled && epoch && epoch != revealed && owned::embedded_sky().installed();
    }
    const auto event = pause.Update(GetTickCount64(), epoch, hold, localMission, paused,
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
