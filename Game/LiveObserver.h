#pragma once
#include <windows.h>
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace LiveObserver {
// Start/stop with the journal; no game state is accessed by the pipe worker.
void Start() noexcept;
void Stop() noexcept;
// Called by the game thread between frames.
void Frame() noexcept;
// Called on the game thread at the completed backbuffer/present boundary.
bool WantsCapture() noexcept;
void CaptureFrame(ID3D11DeviceContext*, IDXGISwapChain*) noexcept;
}
