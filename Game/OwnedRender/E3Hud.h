#pragma once
#include <windows.h>
#include "widescreen_layout.h"
class CI3DScreenMission;
class CI3DObject;
class CI3DSurface;
class CI3DScrollText;
class CI3DToolTip;
class CRenderer;
namespace OwnedRender {
// Used by the native info-window preview; nested figure draws restore the body.
struct E3HudCompositionScope {
    owned::widescreen::HudComposition previous;
    bool visible=false;
    explicit E3HudCompositionScope(CI3DObject* object,int figure=-1) noexcept;
    ~E3HudCompositionScope();
    E3HudCompositionScope(const E3HudCompositionScope&)=delete;
};
bool E3HudItemEventInside(CI3DObject* object,POINT pixel) noexcept;
bool E3HudMapOffset(CI3DObject* object,POINT pixel,POINT& offset) noexcept;
void E3HudCommitVisuals(const CI3DScreenMission* mission);
bool E3MessagePoint(CI3DObject* object,POINT pixel,POINT& native,bool captured,bool& valid) noexcept;
void E3MessageLayout(CI3DScrollText* text);
bool E3HudDraw(CI3DObject* object,CRenderer* renderer);
bool E3HudDraw2D(CI3DObject* object);
bool E3ClockTooltip(CI3DToolTip* tooltip);
bool E3HudBlit(CI3DSurface* surface);
// -2 means an unrelated/non-owned object, -1 means a handled miss.
int E3HudHit(CI3DObject* object,POINT pixel) noexcept;
bool E3HudInside(CI3DObject* object,POINT pixel,bool& inside) noexcept;
bool E3HudAreaInside(CI3DObject* object,unsigned area,POINT pixel,bool& inside) noexcept;
bool E3HudNativePoint(CI3DObject* object,unsigned area,POINT pixel,POINT& native) noexcept;
void E3PublishHud(const CI3DScreenMission* mission,unsigned width,unsigned height) noexcept;
}
