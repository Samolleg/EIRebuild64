#include "StdAfx.h"
#include "E3Input.h"
#include "../GameMain.h"
#include "../I3DMain.h"
#include "../I3DScreen.h"

namespace {
const void* missionOwner = nullptr;
void resetInput() noexcept { theInterface.ResetInputState(); }
}
namespace OwnedRender {
void E3RefreshInterface() noexcept {
    E3PublishInterface(NGame::hWnd, theInterface.GetWidth(), theInterface.GetHeight(),
        theInterface.GetScreensNum() > 0,
        missionOwner && theInterface.GetScreensNum() == 1 &&
            theInterface.GetCurrentScreen() == missionOwner, &resetInput);
}
void E3MissionEnter(const void* owner) noexcept { missionOwner = owner; }
void E3MissionLeave(const void* owner) noexcept {
    if (missionOwner == owner) missionOwner = nullptr;
    E3PublishInterface(NGame::hWnd, theInterface.GetWidth(), theInterface.GetHeight(),
                        false, false, &resetInput);
}
}
