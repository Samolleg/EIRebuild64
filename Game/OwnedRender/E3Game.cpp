#include "StdAfx.h"
#include "../CorePerf.h"
#include "E3Input.h"
#include "E3Hud.h"
#include "E3HudVisual.h"
#include "../I3DScreenMission.h"
#include "../GameMain.h"
#include "../I3DMain.h"
#include "../I3DScreen.h"

namespace {
const void* missionOwner = nullptr;
void resetInput() noexcept { theInterface.ResetInputState(); }
}
namespace OwnedRender {
void E3RefreshInterface() noexcept {
    const bool soleMission = missionOwner && theInterface.GetScreensNum() == 1 &&
        theInterface.GetCurrentScreen() == missionOwner;
    CorePerf::Partition timing(CorePerf::UiPublishInterface);
    E3PublishInterface(NGame::hWnd, theInterface.GetWidth(), theInterface.GetHeight(),
        theInterface.GetScreensNum() > 0, soleMission, &resetInput);
    timing.Stage(CorePerf::UiPublishHud);
    E3PublishHud(soleMission ? static_cast<const CI3DScreenMission*>(missionOwner) : nullptr,
        theInterface.GetWidth(),theInterface.GetHeight());
}
void E3MissionEnter(const void* owner) noexcept {
    // Reset HUD publication/fades even if an allocator reused the previous address.
    E3PublishHud(nullptr,theInterface.GetWidth(),theInterface.GetHeight());
    missionOwner = owner;
}
void E3MissionLeave(const void* owner) noexcept {
    E3HudRetireScreen(owner);
    if (missionOwner == owner) {
        missionOwner = nullptr;
        E3PublishHud(nullptr,theInterface.GetWidth(),theInterface.GetHeight());
    }
    E3PublishInterface(NGame::hWnd, theInterface.GetWidth(), theInterface.GetHeight(),
                        false, false, &resetInput);
}
}
