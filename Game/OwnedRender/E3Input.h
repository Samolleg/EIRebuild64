#pragma once
#include <windows.h>

// Direct publications from CInterface3D; no engine layout or image offsets.
namespace OwnedRender {
void E3PublishInterface(HWND window, unsigned width, unsigned height,
                        bool running, bool soleMission,
                        void (*resetInput)() noexcept) noexcept;
void E3RetireInterface() noexcept;
void E3RefreshInterface() noexcept;
void E3MissionEnter(const void* owner) noexcept;
void E3MissionLeave(const void* owner) noexcept;
void E3HudBoundary() noexcept;
bool E3BlockInput(UINT message, WPARAM parameter) noexcept;
bool E3EditorPaused() noexcept;
HWND E3Window() noexcept;
bool E3Canvas(unsigned& width, unsigned& height) noexcept;
BOOL E3ReadCursor(POINT* point) noexcept;
BOOL E3WriteCursor(int x, int y) noexcept;
}
