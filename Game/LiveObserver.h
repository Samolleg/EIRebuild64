#pragma once
#include <windows.h>
#include <string>
struct ID3D11DeviceContext;
struct IDXGISwapChain;
class CParticle;
class CUnit;
struct CPlot3D;
struct SBoundingVolume;
namespace OwnedRender { struct E1WorldFrame; }
namespace owned {
struct RetainedVisualFrame;
struct EmbeddedArenaInput;
struct EmbeddedGolemInput;
struct EmbeddedCurseInput;
}

namespace LiveObserver {
// Start/stop with the journal; no game state is accessed by the pipe worker.
void Start() noexcept;
void Stop() noexcept;
// Called by the game thread between frames.
void Frame() noexcept;
// Safe command subset while the host is inside a synchronous movie loop.
void MovieFrame() noexcept;
// Render-only camera lease. Ordinary camera state is restored before logic,
// audio, save/load or the next frame's input can inspect it.
class CameraDrawScope {
    bool applied = false;
public:
    CameraDrawScope() noexcept;
    ~CameraDrawScope() noexcept;
    CameraDrawScope(const CameraDrawScope&) = delete;
    CameraDrawScope& operator=(const CameraDrawScope&) = delete;
};
void ResetCamera() noexcept;
// Game-thread synthetic UI cursor; no OS cursor or window reads.
bool ReadUiCursor(POINT*) noexcept;
bool WriteUiCursor(int x,int y) noexcept;
void ReleaseUiCursor() noexcept;
const CPlot3D& LogicObservationPosition(const CPlot3D& ordinary) noexcept;
int ParticleLogicVisibility(const SBoundingVolume&, int renderedVisibility) noexcept;
// These seams copy typed inputs only while render_state/screenshot is pending.
void ObserveWorld(const OwnedRender::E1WorldFrame&) noexcept;
void ObserveVisuals(const owned::RetainedVisualFrame&) noexcept;
void ObserveVisualResult(bool accepted, unsigned long long drawn, unsigned long long omitted) noexcept;
void ObserveParticle(const CParticle&, bool submitted) noexcept;
void ObserveArena(const owned::EmbeddedArenaInput&, HRESULT, bool published) noexcept;
void ObserveGolem(const owned::EmbeddedGolemInput&, HRESULT, bool published) noexcept;
void ObserveCurse(const owned::EmbeddedCurseInput&, HRESULT, bool published) noexcept;
// Transaction notifications, only at existing transition/error call sites.
bool IsOperation(const char* method) noexcept;
void OperationExecuting(const char* method) noexcept;
void OperationApplied(const char* method, bool success) noexcept;
void RecordError(const char* text, const char* extra) noexcept;
// Passive local command receipts; non-MCP orders retain their original path.
bool IsPlayerOrder(int id, int packet) noexcept;
bool CheckPlayerOrder(int id, int packet) noexcept;
void PlayerOrderResult(int id, int packet, const char* error = nullptr) noexcept;
void PlayerOrderRefusal(int id, unsigned code) noexcept;
void ObserveHit(int attacker, int target, float damage, int outcome, int critical) noexcept;
// Process-local protection after the first MCP command; not a config toggle.
std::string ProtectedSaveSlot(const char* name);
// Called on the game thread at the completed backbuffer/present boundary.
bool WantsCapture() noexcept;
void CaptureFrame(ID3D11DeviceContext*, IDXGISwapChain*, unsigned long long frame) noexcept;
}
