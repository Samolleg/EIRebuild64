#pragma once
#include <windows.h>
#include <string>
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace LiveObserver {
// Start/stop with the journal; no game state is accessed by the pipe worker.
void Start() noexcept;
void Stop() noexcept;
// Called by the game thread between frames.
void Frame() noexcept;
// Transaction notifications, only at existing transition/error call sites.
bool IsOperation(const char* method) noexcept;
void OperationExecuting(const char* method) noexcept;
void OperationApplied(const char* method, bool success) noexcept;
void RecordError(const char* text, const char* extra) noexcept;
// Process-local protection after the first MCP command; not a config toggle.
std::string ProtectedSaveSlot(const char* name);
// Called on the game thread at the completed backbuffer/present boundary.
bool WantsCapture() noexcept;
void CaptureFrame(ID3D11DeviceContext*, IDXGISwapChain*) noexcept;
}
