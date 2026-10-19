#pragma once
#include <cstdint>
class CResourceManager;
class CI3DObject;
class CI3DFigure;
class CRenderer;
class CI3DScreenMission;
namespace OwnedRender {
void E3HudDrawPortrait(CI3DFigure* figure, CRenderer* renderer,
    unsigned slot, int unitId, const char* texture, const char* base, bool model) noexcept;
// Numeric values match HudVisualLane; no renderer headers in legacy owners.
enum class E3HudLane { weapon=1, item=2, spell=3, skill=4 };
struct E3HudRoleCreation {
    E3HudRoleCreation(CResourceManager*, bool texture);
    ~E3HudRoleCreation();
    bool Complete(bool success) noexcept;
private:
    bool texture, success=false, active=false;
};
void E3HudResourceChanged(CResourceManager*, bool destroying=false) noexcept;
void E3HudBeginScreen(const CI3DScreenMission*, bool hd) noexcept;
void E3HudCommitScreen(const CI3DScreenMission*, const std::uint32_t* counts) noexcept;
void E3HudRetireScreen(const void*) noexcept;
struct E3HudLanePublication {
    E3HudLanePublication(CI3DObject*, E3HudLane) noexcept;
    ~E3HudLanePublication();
    E3HudLanePublication(const E3HudLanePublication&)=delete;
    E3HudLanePublication& operator=(const E3HudLanePublication&)=delete;
private:
    CI3DObject* previous=nullptr;
    int exceptions=0;
    bool armed=false;
    bool previousCapture=false;
};
void E3HudIcon(E3HudLane, const char* name, unsigned atlas, unsigned sourceWidth,
    unsigned sourceHeight, unsigned width, unsigned height, unsigned x, unsigned y,
    bool hd=false, unsigned sourceX=0, unsigned sourceY=0) noexcept;
bool E3HudAttachFigure(CI3DObject*, const char* name) noexcept;
void E3HudDrawVisual(CI3DFigure*, CRenderer*, E3HudLane, unsigned ordinal) noexcept;
}
