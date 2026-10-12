// 2DScreenWait.cpp: implementation of the C2DScreenWait class.
#include "GameJournal.h"
//
//////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WorldClient.h"
#include "TextureManager.h"
#include "2DScreenWait.h"
#include "dx11/RendererDX11.h"
#include "BinkVideo.h"
#include "EngineDiagnostics.h"
#include "ModCliTool.h"
#include <d3d11_4.h>
#include <atomic>
#include <thread>

#if defined(__VIDEO__) || defined(__GAME__)
#include "I3DCursor.h"
#include "GameMain.h"
#endif // defined(__VIDEO__) || defined(__GAME__)

CRect C2DScreenWait::rectOnScreen;
bool C2DScreenWait::bIgnoreSteps = false;

#ifdef __VIDEO__
HBINK C2DScreenWait::hBink = 0;
DWORD C2DScreenWait::dwSurfaceType = 0;
#endif
extern void LMPTimePassed();

const char *C2DScreenWait::pszProgressName = "progres.bik";
std::unique_ptr<CDXGISurface> C2DScreenWait::pSurface = 0;


namespace {
#ifndef __VIDEO__
std::unique_ptr<CBinkVideo> progressVideo;
std::thread progressThread;
std::atomic<bool> stopProgress{false};
std::atomic<bool> progressFault{false};
Microsoft::WRL::ComPtr<ID3D11Multithread> videoContextLock;
BOOL previousMultithreadProtection = FALSE;
bool videoAttempted = false;
std::string selectedVideo;

class VideoContextGuard {
public:
    explicit VideoContextGuard(ID3D11Multithread *lock) : lock(lock) { lock->Enter(); }
    ~VideoContextGuard() { lock->Leave(); }
private:
    ID3D11Multithread *lock;
};

void ProgressVideoLoop()
{
    try
    {
        while (!stopProgress.load())
        {
            {
                // Gate before the D3D context lock, consistently with the
                // main render path. Never wait here: StopProgressVideo can
                // join us while the main thread owns a nested frame lease.
                const auto renderAccess = theRenderer.GetD3D()->GetOwnedRenderer().TryFrame();
                if (renderAccess.owns_lock() && !stopProgress.load())
                {
                    VideoContextGuard guard(videoContextLock.Get());
                    progressVideo->PlayNextFrame(true, true);
                }
            }
            Sleep(16);
        }
    }
    catch (...)
    { GameJournal::AllocationFailureInCatch("2DScreenWait");
        progressFault = true;
    }
}

void StopProgressVideo()
{
    stopProgress = true;
    if (progressThread.joinable()) progressThread.join();
    progressVideo.reset();
    if (videoContextLock)
    {
        videoContextLock->SetMultithreadProtected(previousMultithreadProtection);
        videoContextLock.Reset();
    }
    selectedVideo.clear();
    progressFault = false;
    videoAttempted = false;
}

// A failed zone load can leave the wait screen without its usual Clear call.
// Join before the static std::thread destructor in that shutdown path.
struct ProgressShutdown {
    ~ProgressShutdown()
    {
        stopProgress = true;
        if (progressThread.joinable()) progressThread.join();
    }
};
ProgressShutdown progressShutdown;

bool StartProgressVideo()
{
    videoAttempted = true;
    selectedVideo = C2DScreenWait::pszProgressName ? C2DScreenWait::pszProgressName : "";
    if (selectedVideo.empty()) return false;

    // The installed mod supplies a movie with this name when present; otherwise
    // CBinkVideo::BeginPlay retains the original game's Movies path.
    CString originalPath = theWorld.GetCDPath() + "Movies\\" + selectedVideo.c_str();
    std::unique_ptr<CBinkVideo> candidate(new CBinkVideo);
    if (!candidate->BeginPlay(originalPath, false, false))
    {
        EngineDiagnostics::Event("Loading video failed name=%s reason=open", selectedVideo.c_str());
        return false;
    }

    candidate->MarkLoadingPicture();
    progressVideo = std::move(candidate);
    auto context = theRenderer.GetD3D()->GetDeviceContext();
    if (SUCCEEDED(context.As(&videoContextLock)) && videoContextLock)
        previousMultithreadProtection = videoContextLock->SetMultithreadProtected(TRUE);
    else
        videoContextLock.Reset();

    // Present a real frame before loading can block the main thread.
    progressVideo->DrawFrame(1, true, true);

    if (videoContextLock)
    {
        stopProgress = false;
        try { progressThread = std::thread(ProgressVideoLoop); }
        catch (const std::exception &)
        { GameJournal::AllocationFailureInCatch("2DScreenWait");
            videoContextLock->SetMultithreadProtected(previousMultithreadProtection);
            videoContextLock.Reset();
        }
    }
    return true;
}
#endif
}

#ifndef __VIDEO__
void C2DScreenWait::LoadLogo()
{
    SMMPFileHeader *pHdr = (SMMPFileHeader *)(theWorld.GetTextureManager()->GetFile("Logo.mmp"));
    if (pHdr)
        pSurface = std::make_unique<CDXGISurface>(theRenderer.GetD3D(), pHdr);
}
#endif

void C2DScreenWait::Clear()
{
#ifdef __VIDEO__
    BinkClose(hBink);
    hBink = 0;
#else
    StopProgressVideo();
#endif
    pSurface = 0;
    theCursor.Show(true);
}

void C2DScreenWait::Step(int nNumb)
{
    if (ModCliTool::GeneratingGraph()) return;
    LMPTimePassed();
    if (bIgnoreSteps) return;
    theCursor.Show(false);
#ifndef __VIDEO__
    if (!videoAttempted)
    {
        try { StartProgressVideo(); }
        catch (const std::exception &error)
        { GameJournal::AllocationFailureInCatch("2DScreenWait");
            EngineDiagnostics::Event("Loading video failed name=%s reason=%s",
                                     selectedVideo.c_str(), error.what());
            StopProgressVideo();
            videoAttempted = true;
        }
    }
    if (progressVideo)
    {
        // A protected immediate context lets the worker present between load
        // steps. If protection is unavailable, advance on the main thread.
        if (!progressThread.joinable())
        {
            progressVideo->PlayNextFrame(true, true);
                }
        return;
    }

    CDX11 *pDX11 = GetRenderer()->GetD3D();
    const auto renderAccess = pDX11->GetOwnedRenderer().LockFrame();
    if (!pSurface)
    {
        LoadLogo();
        rectOnScreen.SetRectEmpty();
        GetRenderer()->GetCurrentMode((DWORD &)rectOnScreen.right, (DWORD &)rectOnScreen.bottom);
        rectOnScreen.top += rectOnScreen.Height() / 4;
        rectOnScreen.bottom -= rectOnScreen.Height() / 3;
    }
    auto m_states = pDX11->GetCommonStates();
    auto sampler = m_states->PointClamp();
    pDX11->BeginFrame();
    pDX11->GetDeviceContext()->PSSetSamplers(0, 1, &sampler);
    pDX11->GetSpriteBatch()->Begin();
    if (pSurface) pSurface->BltToRenderSurface(rectOnScreen, 0, 0);
    pDX11->GetSpriteBatch()->End();
    pDX11->MarkLoadPicture();
    theCursor.Draw();
    pDX11->EndFrame();
#endif
}
