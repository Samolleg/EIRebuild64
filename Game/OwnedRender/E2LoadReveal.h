#pragma once
#include <cstdint>

namespace OwnedRender {
// Renderer load-reveal requests use the game's existing combat-pause command.
// A request belongs to one local mission epoch, never to a saved/new world.
class LoadRevealPause {
    std::uint64_t decided = 0, owner = 0, began = 0;
public:
    enum class Event { none, paused, already_paused, resumed, retry_resume };
    template<class SetPause>
    Event Update(std::uint64_t now, std::uint64_t epoch, bool hold,
                 bool localMission, bool paused, SetPause&& set) {
        if (!localMission || !epoch) { owner = 0; return Event::none; }
        if (owner && owner != epoch) owner = 0;
        if (owner && now - began > 6000) hold = false;
        if (hold && epoch != decided) {
            if (paused) { decided = epoch; return Event::already_paused; }
            if (set(true)) { decided = owner = epoch; began = now; return Event::paused; }
            return Event::none; // Missing player: retry when the source is ready.
        }
        if (!hold && owner == epoch) {
            // Commands are asynchronous. Send the cancellation even if the
            // client has not observed the initial pause yet; retry on failure.
            if (!set(false)) return Event::retry_resume;
            owner = 0; return Event::resumed;
        }
        return Event::none;
    }
};
using SetLoadPause = bool (*)(void*, bool);
bool E2LoadRevealHeld(std::uint64_t epoch) noexcept;
void E2LoadRevealFrame(std::uint64_t epoch, bool localMission, bool paused,
                      SetLoadPause set, void* context) noexcept;
}
