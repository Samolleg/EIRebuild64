#include "E1TextLifetime.h"

namespace {
struct TextOwner {
    const void* owner = nullptr;
    std::uint64_t epoch = 0, frame = 0, draw = 0;
};
TextOwner owners[2];
std::uint64_t nextEpoch = 0, frame = 0, nextDraw = 0;
bool interfaceActive = false;
std::uint64_t advance(std::uint64_t& value) noexcept {
    if (++value == 0) ++value;
    return value;
}
TextOwner& entry(OwnedRender::E1TextKind kind) noexcept {
    return owners[kind == OwnedRender::E1TextKind::pane ? 0 : 1];
}
}
namespace OwnedRender {
void E1TextInterfaceBegin() noexcept {
    owners[0] = {}; owners[1] = {}; interfaceActive = true;
}
void E1TextInterfaceEnd() noexcept {
    owners[0] = {}; owners[1] = {}; interfaceActive = false;
}
void E1TextBeginFrame() noexcept {
    advance(frame);
    for (auto& value : owners) value.frame = value.draw = 0;
}
void E1TextRetire(E1TextKind kind, const void* owner) noexcept {
    auto& value = entry(kind);
    if (!owner || owner == value.owner) value = {};
}
E1TextDraw::E1TextDraw(E1TextKind inputKind, const void* owner) noexcept : kind(inputKind) {
    if (!interfaceActive || !frame || !owner) return;
    auto& value = entry(kind);
    if (value.owner != owner) {
        value = {}; value.owner = owner; value.epoch = advance(nextEpoch);
    }
    value.frame = frame; value.draw = draw = advance(nextDraw);
    stamp = {value.epoch, frame};
}
E1TextDraw::~E1TextDraw() noexcept {
    auto& value = entry(kind);
    if (!complete && draw && value.draw == draw && value.epoch == stamp.epoch)
        value.frame = value.draw = 0;
}
}
namespace owned {
bool bridge_character_pane_text_epoch_current(std::uint64_t epoch, std::uint64_t serial) noexcept {
    if (!interfaceActive || !epoch || !serial || serial != frame) return false;
    for (const auto& value : owners)
        if (value.epoch == epoch && value.frame == serial) return true;
    return false;
}
}
