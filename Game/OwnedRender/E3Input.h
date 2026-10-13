#pragma once
#include <windows.h>
#include <cstdint>

// Direct publications from CInterface3D; no engine layout or image offsets.
namespace OwnedRender {
void E3PublishInterface(HWND window, unsigned width, unsigned height,
                        bool running, bool soleMission,
                        void (*resetInput)() noexcept) noexcept;
void E3RetireInterface() noexcept;
void E3RefreshInterface() noexcept;
void E3MissionEnter(const void* owner) noexcept;
void E3MissionLeave(const void* owner) noexcept;
void E3PublishHover(std::int32_t id, std::uint64_t incarnation) noexcept;
void E3HudBoundary() noexcept;
// Input uses c56 layout only while its real renderer owns the HUD boundary.
bool E3HudRendererAttached() noexcept;
bool E3BlockInput(UINT message, WPARAM parameter) noexcept;
bool E3EditorPaused() noexcept;
HWND E3Window() noexcept;
bool E3Canvas(unsigned& width, unsigned& height) noexcept;
BOOL E3ReadCursor(POINT* point) noexcept;
BOOL E3WriteCursor(int x, int y) noexcept;
}
