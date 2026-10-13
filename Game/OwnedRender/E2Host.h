#pragma once
#include <windows.h>

namespace OwnedRender {
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
bool E2CampInput(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
bool E2BackgroundConfigured() noexcept;
bool E2BackgroundPaused(HWND window) noexcept;
bool E2BlockBackgroundInput(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
DWORD E2Clock(DWORD now, bool paused) noexcept;
}
