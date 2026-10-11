#include "E1TextLifetime.h"
#include <cstdio>
#include <stdexcept>

namespace owned {
bool bridge_character_pane_text_epoch_current(std::uint64_t, std::uint64_t) noexcept;
}
int main() {
    using namespace OwnedRender;
    int checks = 0, pane = 0, chat = 0, stranger = 0;
    auto check = [&](bool value) { if (++checks && !value) throw checks; };
    auto current = [](E1TextStamp s) { return owned::bridge_character_pane_text_epoch_current(s.epoch,s.frame); };
    auto publish = [](E1TextKind kind, const void* owner) {
        E1TextDraw draw(kind,owner); draw.Complete(); return draw.Stamp();
    };
    try {
        check(!current({}));
        check(!publish(E1TextKind::pane,&pane).epoch);
        E1TextInterfaceBegin();
        check(!publish(E1TextKind::pane,&pane).epoch);
        E1TextBeginFrame();
        check(!publish(E1TextKind::pane,nullptr).epoch);
        const auto first = publish(E1TextKind::pane,&pane);
        const auto message = publish(E1TextKind::chat,&chat);
        check(current(first) && current(message));
        check(first.epoch != message.epoch && first.frame == message.frame);
        check(!current({first.epoch,first.frame+1}));
        check(!current({0,first.frame}));
        E1TextRetire(E1TextKind::pane,&stranger);
        check(current(first));
        E1TextBeginFrame();
        check(!current(first) && !current(message));
        const auto next = publish(E1TextKind::pane,&pane);
        check(next.epoch == first.epoch && next.frame != first.frame && current(next));
        E1TextRetire(E1TextKind::pane,&pane);
        check(!current(next));
        const auto reused = publish(E1TextKind::pane,&pane);
        check(reused.epoch != next.epoch && current(reused));
        E1TextStamp aborted;
        try {
            E1TextDraw draw(E1TextKind::pane,&pane); aborted = draw.Stamp();
            check(current(aborted)); throw std::runtime_error("draw failed");
        } catch (const std::runtime_error&) {}
        check(!current(aborted));
        auto replacement = publish(E1TextKind::pane,&stranger);
        {
            E1TextDraw old(E1TextKind::pane,&pane);
            replacement = publish(E1TextKind::pane,&stranger);
        }
        check(current(replacement)); // Aborted old owner cannot revoke its replacement.
        E1TextRetire(E1TextKind::pane);
        E1TextRetire(E1TextKind::chat); // Modal/screen switch in the same frame.
        check(!current(replacement));
        const auto beforeFini = publish(E1TextKind::chat,&chat);
        E1TextInterfaceEnd();
        check(!current(beforeFini));
        check(!publish(E1TextKind::chat,&chat).epoch);
        E1TextInterfaceBegin();
        const auto afterInit = publish(E1TextKind::chat,&chat);
        check(current(afterInit) && afterInit.epoch != beforeFini.epoch);
        E1TextRetire(E1TextKind::chat,&chat);
        check(!current(afterInit));
        E1TextInterfaceEnd();
        std::printf("E1 text lifetime: %d checks PASS\n",checks);
        return 0;
    } catch (int failed) {
        std::fprintf(stderr,"E1 text lifetime check %d FAILED\n",failed); return 1;
    }
}
