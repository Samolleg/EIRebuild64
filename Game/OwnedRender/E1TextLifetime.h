#pragma once
#include "character_pane_owned_text.h"

namespace OwnedRender {
using E1TextKind = owned::OwnedTextKind;
using E1TextStamp = owned::OwnedTextStamp;
void E1TextInterfaceBegin(HFONT combatFont) noexcept;
void E1TextInterfaceEnd() noexcept;
void E1TextBeginFrame() noexcept;
void E1TextRetire(E1TextKind kind, const void* owner = nullptr) noexcept;
class E1TextDraw {
public:
    E1TextDraw(E1TextKind kind, const void* owner, std::uint64_t paneSurface = 0) noexcept;
    ~E1TextDraw() noexcept;
    E1TextDraw(const E1TextDraw&) = delete;
    E1TextDraw& operator=(const E1TextDraw&) = delete;
    E1TextStamp Stamp() const noexcept { return stamp; }
    void Complete() noexcept { complete = true; }
private:
    E1TextStamp stamp;
    bool complete = false;
};
}