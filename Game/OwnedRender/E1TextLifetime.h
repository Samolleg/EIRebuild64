#pragma once
#include <cstdint>

namespace OwnedRender {
enum class E1TextKind { pane, chat };
struct E1TextStamp { std::uint64_t epoch = 0, frame = 0; };
void E1TextInterfaceBegin() noexcept;
void E1TextInterfaceEnd() noexcept;
void E1TextBeginFrame() noexcept;
void E1TextRetire(E1TextKind kind, const void* owner = nullptr) noexcept;
// A stamp remains current until the next frame, owner retirement, or aborted draw.
// Owner pointers are identity only; this adapter never dereferences them.
class E1TextDraw {
public:
    E1TextDraw(E1TextKind kind, const void* owner) noexcept;
    ~E1TextDraw() noexcept;
    E1TextDraw(const E1TextDraw&) = delete;
    E1TextDraw& operator=(const E1TextDraw&) = delete;
    E1TextStamp Stamp() const noexcept { return stamp; }
    void Complete() noexcept { complete = true; }
private:
    E1TextKind kind;
    E1TextStamp stamp;
    std::uint64_t draw = 0;
    bool complete = false;
};
}
