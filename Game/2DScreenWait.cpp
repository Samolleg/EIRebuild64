// 2DScreenWait.cpp: implementation of the C2DScreenWait class.
//
//////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WorldClient.h"
#include "TextureManager.h"
#include "2DScreenWait.h"
#include "dx11/RendererDX11.h"

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

///////////////////////////////////////////////////////////////////////////////////////////////
void StartBinkThread();
void StopBinkThread();
///////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __VIDEO__
void C2DScreenWait::LoadLogo()
{
    SMMPFileHeader *pHdr = (SMMPFileHeader *)(theWorld.GetTextureManager()->GetFile("Logo.mmp"));
    if (pHdr)
    {
        pSurface = std::make_unique<CDXGISurface>(theRenderer.GetD3D(), pHdr);
    }
}
#endif
//////////////////////////////////////////////////////////////////////
void C2DScreenWait::Clear()
{
#ifdef __VIDEO__
    /*
            // CRAP
            StopBinkThread();
            return;
    // CRAP
    */
    BinkClose(hBink);
    hBink = 0;
#endif

    pSurface = 0;
    theCursor.Show(true);
}
//////////////////////////////////////////////////////////////////////
void C2DScreenWait::Step(int nNumb)
{
    LMPTimePassed();
    if (bIgnoreSteps)
        return;
    theCursor.Show(false);
#ifdef __VIDEO__
#else
    CDX11 *pDX11 = GetRenderer()->GetD3D();
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
    // doesn't work
    pSurface->BltToRenderSurface(rectOnScreen, 0, 0);
    pDX11->GetSpriteBatch()->End();
    theCursor.Draw();
    pDX11->EndFrame();
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////
HANDLE g_hExitEvent = 0;
CWinThread *g_pBinkThread = 0;        // streaming thread object pointer
volatile bool g_bShutDownReq = false; // request for shutdown stream thread
///////////////////////////////////////////////////////////////////////////////////////////////
UINT AFX_CDECL BinkThreadStarter(LPVOID pParam)
{
#ifdef __VIDEO__
#endif
    SetEvent(g_hExitEvent);
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////
void StartBinkThread()
{
    if (theWorld.GetTextureManager())
        theWorld.GetTextureManager()->UnloadAllTextures();
    ASSERT(!g_hExitEvent && !g_pBinkThread);
    g_bShutDownReq = false;
    theCursor.Show(false);
    g_hExitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_pBinkThread = AfxBeginThread(BinkThreadStarter, 0, THREAD_PRIORITY_NORMAL);
}
///////////////////////////////////////////////////////////////////////////////////////////////
void StopBinkThread()
{
    if (g_pBinkThread)
    {
        ASSERT(g_hExitEvent);
        g_bShutDownReq = true; // Signal to exit streaming thread
        DWORD dwRes;
        dwRes = WaitForSingleObject(g_hExitEvent, INFINITE);
    }
    CloseHandle(g_hExitEvent);
    g_hExitEvent = 0;
    g_pBinkThread = 0;
    // theCursor.Flip(true);
    theCursor.Show(TRUE);
}
///////////////////////////////////////////////////////////////////////////////////////////////
