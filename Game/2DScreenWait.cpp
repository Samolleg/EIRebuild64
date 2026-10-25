#include "StdAfx.h"
#include "2DScreenWait.h"
#include "MoviePlayback.h"
#include "WorldClient.h"
#include "I3DCursor.h"
#include "ModCliTool.h"
#include "EngineDiagnostics.h"
#include "GameMain.h"

bool C2DScreenWait::bIgnoreSteps = false;
const char* C2DScreenWait::pszProgressName = "progres.bik";
namespace {
// Created/updated/destroyed only on the host thread, never a render worker.
std::unique_ptr<Video::Movie> progressVideo;
bool videoAttempted = false;
}
void C2DScreenWait::Clear() {
    progressVideo.reset();
    videoAttempted = false;
    theCursor.Show(true);
}
void C2DScreenWait::Step(int nNumb) {
    if (ModCliTool::GeneratingGraph() || bIgnoreSteps) return;
    MSG msg;
    if (PeekMessage(&msg, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE)) {
        Clear(); // leave WM_QUIT for the host, join the decoder now
        return;
    }
    theCursor.Show(false);
    if (!videoAttempted) {
        videoAttempted = true;
        try {
            if (!pszProgressName || !*pszProgressName) return;
            const CString path = theWorld.GetCDPath() + "Movies\\" + pszProgressName;
            std::unique_ptr<Video::Movie> candidate(new Video::Movie);
            if (!candidate->BeginPlay(path, false, false, true)) return;
            progressVideo = std::move(candidate);
        } catch (const std::exception& error) {
            EngineDiagnostics::Event("Loading video failed reason=%s", error.what());
            return;
        }
    }
    if (progressVideo) progressVideo->PlayNextFrame(true);
}
