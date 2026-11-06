#include "E2Host.h"
#include "../AgentBackground.h"
#include "E3Input.h"
#include <mutex>
#include "engine_bridge.h"
#include "game_focus.h"
#include <atomic>

namespace {
thread_local OwnedRender::Pass pass = OwnedRender::Pass::unknown;
std::atomic<int> backgroundMode{-1}; // no renderer configuration yet
std::mutex hoverLock;
OwnedRender::E2HoverSelection hovered;
int hoverCursorMode = -1;
}
namespace OwnedRender {
void E2SetHoveredObject(std::int32_t id, std::uint64_t generation, int cursorMode) noexcept {
    std::lock_guard<std::mutex> guard(hoverLock);
    hovered = {id, generation}; hoverCursorMode = cursorMode;
}
void E2ClearHoveredObject() noexcept {
    std::lock_guard<std::mutex> guard(hoverLock);
    hovered = {}; hoverCursorMode = -1;
    E3PublishHover(0, 0);
}
E2HoverSelection E2HoveredObject(int cursorMode) noexcept {
    if (E3EditorPaused() || GetForegroundWindow() != E3Window()) return {};
    std::lock_guard<std::mutex> guard(hoverLock);
    return cursorMode == hoverCursorMode ? hovered : E2HoverSelection{};
}
E2PassScope::E2PassScope(Pass value) noexcept : previous(pass) { pass = value; }
E2PassScope::~E2PassScope() noexcept { pass = previous; }
bool E2BackgroundConfigured() noexcept { return AgentBackground::Enabled() || backgroundMode.load() >= 0; }
bool E2BackgroundPaused(HWND window) noexcept {
    return !AgentBackground::Enabled() && backgroundMode.load() == 1 && window && GetForegroundWindow() != window;
}
bool E2BlockBackgroundInput(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (!E2BackgroundConfigured() || !window) return false;
    const bool input = (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == 0xcc54u;
    const bool altTab = message == WM_SYSKEYUP && wparam == VK_TAB && (lparam & 0x20000000);
    if (AgentBackground::Enabled()) return input || message == WM_INPUT || message == WM_APPCOMMAND;
    return altTab || (input && GetForegroundWindow() != window);
}
DWORD E2Clock(DWORD now, bool paused) noexcept {
    // Same unsigned wraparound contract as timeGetTime. The first resumed
    // tick excludes the whole pause, even if no idle iteration occurred in it.
    static DWORD offset = 0, began = 0;
    static bool wasPaused = false;
    if (paused) {
        if (!wasPaused) { began = now; wasPaused = true; }
        return began - offset;
    }
    if (wasPaused) { offset += now - began; wasPaused = false; }
    return now - offset;
}
}
namespace owned {
SemanticContext current_semantic_context() {
    SemanticContext result{};
    if (pass == OwnedRender::Pass::frame) result.kind = SemanticKind::frame;
    if (pass == OwnedRender::Pass::ui) result.kind = SemanticKind::ui;
    return result; // caller_rva intentionally absent in direct host calls
}
void set_background_mode(int mode) noexcept {
    backgroundMode.store(mode == static_cast<int>(BackgroundMode::pause) ? 1 : 0);
}
}
