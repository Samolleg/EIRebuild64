#include "E1TextLifetime.h"
#include "../GameJournal.h"

namespace {
void textError(const char* reason) noexcept {
    GameJournal::Event("render", "owned_text_omitted", "reason=%s", reason);
}
}

namespace OwnedRender {
void E1TextInterfaceBegin(HFONT font) noexcept { owned::owned_text_interface_begin(font, &textError); }
void E1TextInterfaceEnd() noexcept { owned::owned_text_interface_end(); }
void E1TextBeginFrame() noexcept { owned::owned_text_begin_frame(); }
void E1TextRetire(E1TextKind kind, const void* owner) noexcept { owned::owned_text_retire(kind, owner); }
E1TextDraw::E1TextDraw(E1TextKind kind, const void* owner, std::uint64_t surface) noexcept
    : stamp(owned::owned_text_begin(kind, owner, surface)) {}
E1TextDraw::~E1TextDraw() noexcept { owned::owned_text_complete(stamp, complete); }
}
namespace owned {
bool bridge_character_pane_text_epoch_current(std::uint64_t epoch, std::uint64_t frame) noexcept {
    return owned_text_epoch_current(epoch, frame);
}
}
