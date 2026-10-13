#include "E2LoadReveal.h"
#include "engine_bridge.h"
#include "embedded_sky.h"
#include "../GameJournal.h"
#include <cstdio>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    using namespace OwnedRender;
    using Event = LoadRevealPause::Event;
    unsigned checks = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) throw checks; };
    try {
        LoadRevealPause policy;
        std::vector<bool> commands;
        bool available = true;
        auto set = [&](bool value) { if (!available) return false; commands.push_back(value); return true; };
        check(policy.Update(0, 1, true, false, false, set) == Event::none && commands.empty());
        check(policy.Update(0, 0, true, true, false, set) == Event::none && commands.empty());
        check(policy.Update(0, 1, true, true, true, set) == Event::already_paused && commands.empty());
        check(policy.Update(1, 1, false, true, true, set) == Event::none && commands.empty());
        // A player's/save's pause is never released, nor acquired after they resume.
        check(policy.Update(2, 1, true, true, false, set) == Event::none && commands.empty());
        available = false;
        check(policy.Update(3, 2, true, true, false, set) == Event::none && commands.empty());
        available = true;
        check(policy.Update(4, 2, true, true, false, set) == Event::paused && commands.size() == 1 && commands.back());
        check(policy.Update(5, 2, true, true, false, set) == Event::none && commands.size() == 1);
        available = false;
        check(policy.Update(6, 2, false, true, false, set) == Event::retry_resume && commands.size() == 1);
        available = true;
        // Reveal arrived before the server acknowledged pause: cancel pending request.
        check(policy.Update(7, 2, false, true, false, set) == Event::resumed && commands.size() == 2 && !commands.back());
        check(policy.Update(8, 2, false, true, true, set) == Event::none && commands.size() == 2);
        check(policy.Update(9, 2, true, true, false, set) == Event::none && commands.size() == 2);
        check(policy.Update(10, 3, true, true, false, set) == Event::paused);
        check(policy.Update(6010, 3, true, true, true, set) == Event::none);
        check(policy.Update(6011, 3, true, true, true, set) == Event::resumed && !commands.back());
        check(policy.Update(6012, 3, true, true, false, set) == Event::none);
        check(policy.Update(6013, 4, true, true, false, set) == Event::paused);
        const auto count = commands.size();
        check(policy.Update(6014, 5, false, true, true, set) == Event::none && commands.size() == count);
        check(policy.Update(6015, 5, true, true, true, set) == Event::already_paused);
        check(policy.Update(6016, 6, true, true, false, set) == Event::paused);
        const auto localCount = commands.size();
        check(policy.Update(6017, 6, false, false, true, set) == Event::none && commands.size() == localCount);
        check(policy.Update(6018, 6, false, true, true, set) == Event::none && commands.size() == localCount);

        GameJournal::TestStart(argv[1]);
        commands.clear();
        auto command = [](void* context, bool value) -> bool {
            static_cast<std::vector<bool>*>(context)->push_back(value); return true;
        };
        owned::bridge_set_load_reveal(true, 0);
        E2LoadRevealFrame(101, true, false, command, &commands);
        check(commands.empty()); // No initialized embedded renderer.
        int owner = 0;
        check(owned::embedded_sky().attach(&owner));
        E2LoadRevealFrame(101, false, false, command, &commands); check(commands.empty());
        E2LoadRevealFrame(101, true, false, command, &commands);
        check(commands.size() == 1 && commands.back());
        owned::bridge_set_load_reveal(true, 101);
        E2LoadRevealFrame(101, true, false, command, &commands);
        check(commands.size() == 2 && !commands.back());
        E2LoadRevealFrame(102, true, true, command, &commands); check(commands.size() == 2);
        owned::bridge_set_load_reveal(false, 0);
        E2LoadRevealFrame(102, true, true, command, &commands); check(commands.size() == 2);
        owned::bridge_set_load_reveal(true, 0);
        E2LoadRevealFrame(103, true, false, command, &commands); check(commands.size() == 3 && commands.back());
        owned::embedded_sky().detach(&owner);
        E2LoadRevealFrame(103, true, true, command, &commands); check(commands.size() == 4 && !commands.back());
        GameJournal::TestDrain(); GameJournal::Stop();
        std::printf("E2_LOAD_REVEAL_PASS checks=%u\n", checks); return 0;
    } catch (unsigned failed) { GameJournal::Stop(); std::printf("E2_LOAD_REVEAL_FAIL check=%u\n", failed); return 1; }
}
