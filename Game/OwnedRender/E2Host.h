#pragma once
#include <windows.h>
#include <cstdint>

namespace OwnedRender {
// Consume the first fatal backend error outside renderer code.
bool E2TakeD3D12Failure(char (&text)[2048]) noexcept;
// Direct host scopes; no retail return addresses or persisted world pointers.
enum class Pass { unknown, frame, ui };
class E2PassScope {
    Pass previous;
public:
    explicit E2PassScope(Pass pass) noexcept;
    ~E2PassScope() noexcept;
    E2PassScope(const E2PassScope&) = delete;
    E2PassScope& operator=(const E2PassScope&) = delete;
};
struct E2HoverSelection {
    std::int32_t id = 0;
    std::uint64_t generation = 0;
};
void E2SetHoveredObject(std::int32_t id, std::uint64_t generation, int cursorMode) noexcept;
void E2ClearHoveredObject() noexcept;
E2HoverSelection E2HoveredObject(int cursorMode) noexcept;
bool E2CampInput(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
bool E2BackgroundConfigured() noexcept;
bool E2BackgroundPaused(HWND window) noexcept;
bool E2BlockBackgroundInput(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
DWORD E2Clock(DWORD now, bool paused) noexcept;
}
