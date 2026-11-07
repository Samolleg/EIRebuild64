#pragma once

class CRenderer;
class CI3DFigure;
class CI3DScreenGlobalMap;

namespace OwnedRender::GlobalMapPreview {
// Game-thread ingress only. Resources/geometry and marker morphs stay renderer-owned.
void Precache(CRenderer* renderer) noexcept;
void Activate(CI3DScreenGlobalMap* screen, CRenderer* renderer) noexcept;
void Deactivate(CI3DScreenGlobalMap* screen, CRenderer* renderer) noexcept;
bool SubmitSection(CI3DFigure* figure, CRenderer* renderer) noexcept;

class DrawScope {
    CI3DScreenGlobalMap* previous = nullptr;
    bool previousEnabled = false, enabled = false, ready = true;
    CRenderer* renderer = nullptr;
public:
    DrawScope(CI3DScreenGlobalMap* screen, CRenderer* renderer) noexcept;
    ~DrawScope();
    bool Ready() const noexcept { return ready; }
    DrawScope(const DrawScope&) = delete;
    DrawScope& operator=(const DrawScope&) = delete;
};
class MarkerScope {
    CRenderer* renderer = nullptr;
public:
    MarkerScope(CI3DFigure* figure, CRenderer* renderer) noexcept;
    ~MarkerScope();
    MarkerScope(const MarkerScope&) = delete;
    MarkerScope& operator=(const MarkerScope&) = delete;
};
class HitTestScope {
public:
    HitTestScope() noexcept;
    ~HitTestScope();
    HitTestScope(const HitTestScope&) = delete;
    HitTestScope& operator=(const HitTestScope&) = delete;
};
}
