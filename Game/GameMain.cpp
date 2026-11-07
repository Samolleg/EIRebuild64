#include "StdAfx.h"
#include "RuntimePaths64.h"
#include "SaveSlot.h"
#include "CorePerf.h"
#include "LiveObserver.h"
#include "AgentBackground.h"
#include "OwnedRender/E4Camp.h"
#include "OwnedRender/E3Input.h"
#include <game_focus.h>
#include <cursor_confinement.h>
#include "GameJournal.h"
#if defined(__SANITIZE_ADDRESS__)
#include "ASanReporting64.inl"
#endif
#include "ErrorDialog.h"
#include <cursor_image.h>
#include "SettingsProfile.h"
#include "ModRandomOwnershipCheck.inl"
#include "ModMobFlagArgumentsCheck.inl"
#include "ModItemSpellPolicyCheck.inl"
#include "ModStealingPolicyCheck.inl"
#include "ModNpcEquipmentCheck.inl"
#include "ModHitHistoryCheck.inl"
#include "ModItemUiCheck.inl"
#include "ModSettingsCheck.inl"
#include "ModDiagnosticsCheck.inl"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "EngineDiagnostics.h"
#include "FrameDiagnostics.h"
#include "ModShadowSchedule.h"
#include "ModWorldBackground/Native.h"
#include <stdexcept>
#include <io.h>
#include <new.h>

#include "dx11/RendererDX11.h"
#include "GameMain.h"
#include "OwnedRender/Startup.h"
#include "OwnedRender/E2Host.h"
#include "OwnedRender/E1WorldLifecycle.h"
#include "OwnedRender/E1TextLifetime.h"
#include "StringProcessing.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "WorldInterface.h"
#include "ScenarioTracker.h"
#include "Camera.h"
#include "CameraTerrainCollision.h"
#include "Renderer.h"
#include "PerkDatabase.h"
#include "QuestDatabase.h"
#include "UnitDatabase.h"
#include "SpellDatabase.h"
#include "ItemDatabase.h"
#include "LeverDatabase.h"
#include "AckDatabase.h"
#include "TerrainPrintsDatabase.h"
#include "I3DMain.h"
#include "A3Cursor.h"
#include "MoviePlayback.h"
#include "aiSettings.h"
#include "Console.h"
#include "DXGeometryProcessor.h"
#include "TextureManager.h"
#include "PlayerClient.h"
#include "IniFile.h"
#include "2DScreenWait.h"
#include "DebugAlloc.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "ModCli.inl"
#include "SettingsProfileCheck.inl"
/////////////////////////////////////////////////////////////////////////////////////
// __DEMO__,__SINGLEONLY__,NODEBUGINFO,DONT_DISPLAY_DEBUG_IDS
// ,NOGETDC,NOBLIT,SURFACE_BACKUP
// #define USE_SETTINGS_INI 1
/*#define DONT_DISPLAY_DEBUG_IDS
#define DONT_USE_SETTINGS_INI 1*/
/////////////////////////////////////////////////////////////////////////////////////
static inline double GetI64(const ULARGE_INTEGER *t)
{
    return static_cast<double>(t->QuadPart);
}

double NGame::GetFreeDiskSpace(const char *pszDrive)
{
    typedef BOOL(WINAPI * GDFSE)(LPCTSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);

    GDFSE pfnGetDiskFreeSpaceEx = (GDFSE)GetProcAddress(GetModuleHandle("kernel32.dll"),
                                                        "GetDiskFreeSpaceExA");
    BOOL bRetVal = FALSE;
    double fRetVal = 0;
    if (pfnGetDiskFreeSpaceEx)
    {
        ULARGE_INTEGER i64FreeBytesToCaller, i64TotalBytes, i64FreeBytes;
        bRetVal = (*pfnGetDiskFreeSpaceEx)(pszDrive, &i64FreeBytesToCaller, &i64TotalBytes,
                                           &i64FreeBytes);
        fRetVal = GetI64(&i64FreeBytesToCaller);
    }
    else
    {
        DWORD dwSectPerClust, dwBytesPerSect, dwFreeClusters, dwTotalClusters;
        bRetVal = GetDiskFreeSpace(pszDrive, &dwSectPerClust, &dwBytesPerSect, &dwFreeClusters,
                                   &dwTotalClusters);
        fRetVal = double(dwFreeClusters) * double(dwSectPerClust) * double(dwBytesPerSect);
    }

    return (bRetVal == 0 ? 0 : fRetVal);
}

bool NGame::DumpMemUsage(bool bByFile, const char *pszFileName)
{
    SExternalDebugAllocStat *stats = 0;
    int nNumStats = 0;
    if (!GetAllocStats(stats, nNumStats))
        return false;
    // collect total mem usage
    unsigned int uUsedMemory = 0;
    for (int i = 0; i < nNumStats; ++i)
        uUsedMemory += stats[i].uAllocatedMemory - stats[i].uFreedMemory;
    //
    OutputDebugString("\n********************Begin Memory Usage Dump********************\n\n");
    if (bByFile)
    {
        std::map<std::string, DWORD> allocs;
        for (int i = 0; i < nNumStats; ++i)
            allocs[stats[i].pszFileName] += stats[i].uAllocatedMemory - stats[i].uFreedMemory;
        //
        if (pszFileName == 0)
        {
            char buff[1024];
            for (auto pos = allocs.begin(); pos != allocs.end(); ++pos)
            {
                if (pos->second == 0)
                    continue;
                sprintf(buff, "%s: %d bytes\n", pos->first.c_str(), pos->second);
                OutputDebugString(buff);
            }
        }
    }
    else
    {
        char buff[1024];
        for (int i = 0; i < nNumStats; ++i)
        {
            if (stats[i].uAllocatedMemory - stats[i].uFreedMemory == 0)
                continue;
            if (!(((pszFileName != 0) && (strcmp(stats[i].pszFileName, pszFileName) == 0)) ||
                  (pszFileName == 0)))
                continue;
            sprintf(buff, "%s(%d): %d bytes\n", stats[i].pszFileName, stats[i].uLineNumber,
                    stats[i].uAllocatedMemory - stats[i].uFreedMemory);
            OutputDebugString(buff);
        }
    }
    // total memory usage
    {
        OutputDebugString("-----------------------------------------------------------\n");
        char buff[1024];
        sprintf(buff, "Total memory usage: %d bytes (~%dMB)\n", uUsedMemory,
                uUsedMemory / (1024 * 1024));
        OutputDebugString(buff);
    }
    //
    delete[] stats;
    OutputDebugString("\n********************End Memory Usage Dump********************\n\n");
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// MEMORY counting code
/////////////////////////////////////////////////////////////////////////////////////
DWORD GetDXVersion();
/////////////////////////////////////////////////////////////////////////////////////
struct _CrtMemBlockHeader
{
    // Pointer to the block allocated just before this one:
    struct _CrtMemBlockHeader *pBlockHeaderNext;
    // Pointer to the block allocated just after this one:
    struct _CrtMemBlockHeader *pBlockHeaderPrev;
    char *szFileName; // File name
    int nLine;        // Line number
    size_t nDataSize; // Size of user block
    int nBlockUse;    // Type of block
    long lRequest;    // Allocation number
    // Buffer just before (lower than) the user's memory:
    // unsigned char gap[nNoMansLandSize];
};

struct SAllocInfo
{
    char pszFileName[256];
    int nAllocNum;
    int nAllocSize;
};

int __cdecl AllocInfoCompare(const void *elem1, const void *elem2)
{
    if (((SAllocInfo *)elem1)->nAllocSize < ((SAllocInfo *)elem2)->nAllocSize)
        return 1;
    if (((SAllocInfo *)elem1)->nAllocSize > ((SAllocInfo *)elem2)->nAllocSize)
        return -1;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void TypeMemoryUsage()
{
#ifdef _DEBUG
    std::vector<SAllocInfo> info(1024, SAllocInfo{});

    // FILE *f = fopen( "Strings.txt", "wt" );
    _CrtMemBlockHeader *pCur;
    _CrtMemState state;
    _CrtMemCheckpoint(&state);

    int nFileMax = 0;
    for (pCur = state.pBlockHeader; pCur; pCur = pCur->pBlockHeaderNext)
    {
        if (pCur->nBlockUse != _NORMAL_BLOCK)
            continue;
        const char *pszFileName = "Unknown";
        if (pCur->szFileName)
            pszFileName = pCur->szFileName;
        int i = 0;
        for (; i < nFileMax; i++)
            if (strcmp(info[i].pszFileName, pszFileName) == 0)
                break;
        if (i == nFileMax)
        {
            nFileMax++;
            strcpy(info[i].pszFileName, pszFileName);
            info[i].nAllocNum = 0;
            info[i].nAllocSize = 0;
        }
        /*
        if ( strcmp( pszFileName + strlen(pszFileName) - 11, "strcore.cpp" ) == 0 )
        {
                const char *pStr = ((const char*)pCur)+44;
                fwrite( pStr, strlen(pStr), 1, f );
                fwrite( "\n", 1, 1, f );
        }*/
        info[i].nAllocNum++;
        info[i].nAllocSize += pCur->nDataSize;
    }
    // fclose( f );

    TRACE0("Current memory usage:\n");
    qsort(info.data(), nFileMax, sizeof(SAllocInfo), AllocInfoCompare);
    for (int i = 0; i < nFileMax; i++)
    {
        char buf[1024];
        sprintf(buf, " File \"%s\": Number of blocks: %i, Number of bytes: %i\n",
                info[i].pszFileName, info[i].nAllocNum, info[i].nAllocSize);
        TRACE0(buf);
    }
#endif
}
/////////////////////////////////////////////////////////////////////////////////////
// namespace, which represent all global Windows-related variables and functions
// this file additions to the NGame
namespace NGame {
const DWORD MAX_LOADSTRING = 256;
// Windows identification variables
HWND hWnd;                            // window handler
HINSTANCE hInstance;                  // current instance
TCHAR szAppTitleName[MAX_LOADSTRING]; // application title ( will be loaded during initialization )
TCHAR szWndClassName[MAX_LOADSTRING]; // user window class name ( will be loaded during
                                      // initialization )
ATOM atomWndClassName; // atom window class name identification (assigned during registration)
bool bNeedRender = true; // crap
DWORD dwMovieFrameNumber = 0;
//
CString szCameraFileName;
CString szScriptFileName;
CString szObjectsFileName;
CString szMapFileName;
CString szMovieDir(".\\capture\\");
//
bool bEnableRender = false;
bool bEnableRenderStateChanged = false;
bool bEnableQuit = true;
bool bEnableFlip = true;
//
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ShortWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void UpdateRenderEnable();
bool ProcessMessages();
void EnableFlip(bool bVal)
{
    bEnableFlip = bVal;
}
bool IsFlipEnable()
{
    return bEnableFlip;
}
//
BOOL LoadGame();
}; // namespace NGame
using namespace NGame;
/////////////////////////////////////////////////////////////////////////////////////
void NGame::AllowQuit(bool bAllow)
{
    bEnableQuit = bAllow;
}
/////////////////////////////////////////////////////////////////////////////////////
bool NGame::CanQuit() noexcept { return bEnableQuit; }
bool NGame::QuitProgram(int nExitCode)
{
    if (ErrorDialog::RequestClose()) return true;
    if (bEnableQuit)
    {
        PostQuitMessage(nExitCode);
        return true;
    }
    else
        return false;
}
/////////////////////////////////////////////////////////////////////////////////////
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage is only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
ATOM NGame::MyRegisterClass(HINSTANCE hInst)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = reinterpret_cast<WNDPROC>(ShortWndProc);
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInst;
    wcex.hIcon = LoadIcon(hInst, reinterpret_cast<LPCTSTR>(IDI_MAIN));   // main icon
    wcex.hIconSm = LoadIcon(hInst, reinterpret_cast<LPCTSTR>(IDI_MAIN)); // small icon
    wcex.hCursor = 0; // LoadCursor( NULL, IDC_ARROW ); <= we don't need cursor in our window!
    wcex.hbrBackground =
        0; // reinterpret_cast<HBRUSH>( COLOR_WINDOW + 1 ); <= we don't need any color filling
    wcex.lpszMenuName = 0; // <= no menu
    wcex.lpszClassName = szWndClassName;

    return RegisterClassEx(&wcex);
}
/////////////////////////////////////////////////////////////////////////////////////
//   FUNCTION: InitInstance(HANDLE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
BOOL NGame::InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    // Store instance handle in our global variable
    NGame::hInstance = hInstance;
    if (AgentBackground::Enabled()) {
        // A real, visible window below the owner's windows. Never activate it,
        // even when display settings request borderless/fullscreen.
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS;
        const DWORD extra = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        RECT work{};
        if (!SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0)) return FALSE;
        RECT frame{}; if (!AdjustWindowRectEx(&frame, style, FALSE, extra)) return FALSE;
        const LONG availableWidth = work.right-work.left-(frame.right-frame.left);
        const LONG availableHeight = work.bottom-work.top-(frame.bottom-frame.top);
        if (availableWidth <= 0 || availableHeight <= 0) return FALSE;
        theSettings.dwWidth = (std::min)(theSettings.dwWidth, DWORD(availableWidth));
        theSettings.dwHeight = (std::min)(theSettings.dwHeight, DWORD(availableHeight));
        RECT outer{0,0,LONG(theSettings.dwWidth),LONG(theSettings.dwHeight)};
        if (!AdjustWindowRectEx(&outer, style, FALSE, extra)) return FALSE;
        hWnd = CreateWindowEx(extra, LPCSTR(atomWndClassName), szAppTitleName, style,
            work.left, work.top, outer.right-outer.left, outer.bottom-outer.top,
            nullptr, nullptr, hInstance, nullptr);
        if (!hWnd) return FALSE;
        bEnableRender = true; bEnableRenderStateChanged = true;
        return SetWindowPos(hWnd, HWND_BOTTOM, work.left, work.top, outer.right-outer.left,
            outer.bottom-outer.top, SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE;
    }
    // create window with sysmenu on the taskbar
    hWnd = CreateWindowEx(0, LPCSTR(atomWndClassName), szAppTitleName,
                          /*WS_OVERLAPPED | WS_SYSMENU */ WS_POPUP /*| WS_SYSMENU*/ |
                              WS_CLIPSIBLINGS,
                          0, 0, theSettings.dwWidth, theSettings.dwHeight, 0, 0, hInstance, 0);
    // return with FALSE if window wasn't created
    if (!hWnd)
        return FALSE;
    // show & update window
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    // eliminate cursor once for this widow
    SetCursor(0);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NGame::CheckPreviousApp(LPCSTR pszMainClass, LPCSTR pszMainTitle)
{
    return TRUE;
    HWND hwndFind, hwndLast, hwndForeGround;
    DWORD dwFindID, dwForeGroundID;
    // Check if application is already running
    hwndFind = FindWindow(pszMainClass, pszMainTitle);
    if (hwndFind)
    {
        // Bring previously running application's main
        // window to the user's attention
        hwndForeGround = GetForegroundWindow();
        dwForeGroundID = GetWindowThreadProcessId(hwndForeGround, 0);
        dwFindID = GetWindowThreadProcessId(hwndFind, 0);

        // Don't do anything if window is already in foreground
        // Unless it is iconized.
        if ((dwFindID != dwForeGroundID) || IsIconic(hwndFind))
        {
            hwndLast = GetLastActivePopup(hwndFind);
            if (IsIconic(hwndLast))
                ShowWindow(hwndLast, SW_RESTORE);

            BringWindowToTop(hwndLast);
            SetForegroundWindow(hwndLast);
        }

        // Prevent additional instance's of this application
        // from running
        return FALSE;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
int NGame::MessageBox(LPCSTR pszText, UINT uType, bool continuing)
{
    GameJournal::Event("ERROR", "message_box", "type=%u text=%s", uType, GameJournal::Quote(pszText).c_str());
    GameJournal::FlushCritical();
    const int result = ErrorDialog::ShowA(hWnd, pszText, szAppTitleName, uType, continuing);
    GameJournal::Event("ERROR", "message_box_closed", "result=%d continuing=%d", result, continuing);
    GameJournal::FlushCritical();
    return result;

}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NGame::LoadGame()
{
    // load terrain
    CString szTerrainFileName;
    if (!szMapFileName.IsEmpty())
        szTerrainFileName = szMapFileName;
    else
        szTerrainFileName = theSettings.szDefaultMapName;

    if (szObjectsFileName == "")
    {
        // action on startup
        IWorldConnector.ReqGoMainMenu();
        IWorldConnector.DoGo();
        // IWorld.NewGame();
    }
    else
    {
        if (theWorldServer.NewZone(szTerrainFileName, szObjectsFileName) == FALSE)
            return ReportError("Can't load map \"%s\".\n", szTerrainFileName);
        theWorldServer.StartServer(SERV_MODE_LOCAL);
        theWorld.ConnectLocal(theWorldServer.GetLocalNetDriver());
        theWorldServer.curInterface = IFACE_DEBUG;
        if (!szScriptFileName.IsEmpty())
        {
            if (theWorldServer.LoadScript(CString(theWorld.GetWorkPath()) + "Scripts\\" +
                                          szScriptFileName) == FALSE)
            {
                SScriptError scriptError = theWorldServer.script.GetLastError();
                return ReportError("%s, line %i", scriptError.text, scriptError.nLine);
            }
        }
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
int nShowClientTimeBalance = 0;
/////////////////////////////////////////////////////////////////////////////////////
// function advances current game time and processes segments
// time sync problem is to be refined
// Scheduling hint only: CountTime remains the sole owner of elapsed time and
// segment accumulators. Never advance simulation by a requested wait duration.
static DWORD idleLogicDue = 0;
static bool CountTime()
{
    CorePerf::Scope coreTiming(CorePerf::CountTime);
    static DWORD dwLastTime = OwnedRender::E2Clock(timeGetTime(), OwnedRender::E2BackgroundPaused(NGame::hWnd));
    static DWORD dwPassedTime = 0;
    static DWORD dwClientPassedTime = 0;
    static float fpClientTimeSpeed = 1;

    DWORD dwTimeStep = OwnedRender::E2Clock(timeGetTime(), OwnedRender::E2BackgroundPaused(NGame::hWnd)) - dwLastTime;
    FrameDiagnostics::RawDelta(dwTimeStep);
    DWORD dwFracTime; // for client
    dwLastTime += dwTimeStep;
    idleLogicDue = dwLastTime + DW_SEGMENT_TIME;
    theCamera.UpdateFieldCampCamera();
    OwnedRender::E3RefreshInterface();
    CameraTerrainCollision::BeginFrame(
        !OwnedRender::E2BackgroundPaused(NGame::hWnd) && !OwnedRender::E3EditorPaused() &&
        GetForegroundWindow() == NGame::hWnd && !IsIconic(NGame::hWnd),
        DW_SEGMENT_TIME, 1.0f / static_cast<float>(DW_DEFAULT_SEGMENT_TIME));
    if (OwnedRender::E3EditorPaused()) return true;
    // check if focus is in application
    /*if (!GetRenderer()->IsActive())
    {
        IWorld.SetActiveState(false);
        return false;
    }*/
    IWorld.SetActiveState(true);
    //
    if (theWorld.IsFailedConnection())
    {
        {
            ASSERT(0);
            // MessageBox( NULL, "Client side connection lost", "ERROR", MB_OK | MB_ICONERROR );
            IWorldConnector.ReqGoMainMenu();
        }
    }
    // calc general timestep
    if (dwTimeStep > 0x80000000)
        dwTimeStep = 0;
    // limit maximal passed time
    if (dwTimeStep > DW_SEGMENT_TIME * 5)
        dwTimeStep = DW_SEGMENT_TIME * 5;
    /*if (b25fpsMode)
        dwTimeStep = int(ceil(1000.0f / float(theSettings.nMovieCapturingFPS)));*/
    dwPassedTime += dwTimeStep;
    // calc client timestep
    if (!theWorld.IsConnected())
        fpClientTimeSpeed = 1;
    dwClientPassedTime += fpClientTimeSpeed * dwTimeStep;
    ASSERT(dwClientPassedTime < 0x4000000);
    if (dwClientPassedTime > 0x4000000)
        dwClientPassedTime = 0;

    // segments processing
    dwFracTime = 0;
    for (;;)
    {
        bool bBreak = true;
        if (dwPassedTime >= DW_SEGMENT_TIME)
        {
            bBreak = false;
            if (!theWorldServer.Segment())
                return true;
            dwPassedTime -= DW_SEGMENT_TIME;
        }

        if (dwClientPassedTime >= DW_SEGMENT_TIME)
        {
            if (theWorld.HasPacket() || !theWorld.IsConnected())
            {
                bBreak = false;
                if (!theWorld.Segment())
                    return true;
                dwClientPassedTime -= DW_SEGMENT_TIME;
            }
        }

        if (bBreak)
            break;
    }

    IWorldConnector.DoGo();
    // setup client frac position
    dwFracTime += dwClientPassedTime;
    float fpFracTime = float(dwFracTime) / float(DW_SEGMENT_TIME);
    if (!theWorld.IsCombatPause())
        theWorld.SetTimeFraction(fpFracTime);
    //	here we make hard decision about changing speed of time on client
    // CRAP very first estimation, it is bad because it is fps dependent and does not account
    // statistics
    float fpPacketBalance = theWorld.GetInputPacketsNum() - fpFracTime;
    if (fpPacketBalance < 0)
        fpClientTimeSpeed = (float)exp(fpPacketBalance * 0.1f);
    else if (fpPacketBalance > 1)
        fpClientTimeSpeed = 1 + ((float)sqrt(fpPacketBalance)) * 0.1f - 0.1f;
    else
        fpClientTimeSpeed = 1;
    fpClientTimeSpeed = Max(fpClientTimeSpeed, 0.1f);
    fpClientTimeSpeed = Min(fpClientTimeSpeed, 10.0f);
    if (nShowClientTimeBalance)
        NConsole::net << "Client speed = " << fpClientTimeSpeed << " balance = " << fpPacketBalance
                      << endl;

    // Advance the camera only by wall-clock time. Client segments above already
    // consume that same interval and must not add another camera step.
    while (dwTimeStep > DW_SEGMENT_TIME)
    {
        CameraTerrainCollision::AdvanceCamera(&theCamera, static_cast<float>(DW_SEGMENT_TIME) /
                       static_cast<float>(DW_DEFAULT_SEGMENT_TIME));
        dwTimeStep -= DW_SEGMENT_TIME;
    }
    CameraTerrainCollision::AdvanceCamera(&theCamera, static_cast<float>(dwTimeStep) / static_cast<float>(DW_DEFAULT_SEGMENT_TIME));

    // save camera position
    if (!theWorld.IsCombatPause())
    {
        if (theCamera.CheckFlag(CAM_SAVE_PLACEMENTS))
            theCamera.SaveCurrentCameraPlacement(theWorld.GetPreciseTime());
        if (theCamera.CheckFlag(CAM_USE_PLACEMENTS))
            theCamera.PlayCamera(theWorld.GetPreciseTime());
    }
    // Earliest existing server/client segment deadline. Waiting for an input
    // packet is still serviced by the server deadline; no network state changes.
    DWORD wait = dwPassedTime >= DW_SEGMENT_TIME ? 0 : DW_SEGMENT_TIME - dwPassedTime;
    if (theWorld.HasPacket() || !theWorld.IsConnected()) {
        const DWORD clientWait = dwClientPassedTime >= DW_SEGMENT_TIME ? 0 :
            static_cast<DWORD>(std::ceil(double(DW_SEGMENT_TIME - dwClientPassedTime) / fpClientTimeSpeed));
        wait = (std::min)(wait, clientWait);
    }
    idleLogicDue = dwLastTime + wait;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
static void NGame::UpdateRenderEnable()
{
    if (bEnableRenderStateChanged)
    {
        if (bEnableRender)
        {
            if (theRenderer.IsInited()) // can restore surfaces only for initialized visualizator
                GetRenderer()->SetFlag(VIS_ACTIVE);
            if (!AgentBackground::Enabled()) SetCursor(0);
        }
        else
        {
            GetRenderer()->ResetFlag(VIS_ACTIVE);
            InvalidateRect(0, 0, TRUE);
        }
        bEnableRenderStateChanged = false;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// DrawFrame has no frame-admission poll: an admitted frame already blocks in
// the renderer's existing scene/present/limiter waits. Only its fast rejection
// paths need an idle wait. Renderer logic and FPS pacing stay with the renderer.
static void WaitForIdleWork()
{
    if (OwnedRender::E2BackgroundPaused(NGame::hWnd))
        return; // DrawFrame already used the established Sleep(50) path.
    if (NGame::bEnableRender && NGame::bNeedRender) {
        if (!theInterface.GetCurrentScreen()) return; // Existing Sleep(50).
        if (theRenderer.IsActive()) return; // Admissible now; no second limiter.
    }
    // A missing screen does not imply Sleep(50): disabled/not-needed exits
    // precede that check in DrawFrame (notably during DestroyScreens).
    const DWORD now = OwnedRender::E2Clock(timeGetTime(), false);
    const LONG remaining = static_cast<LONG>(idleLogicDue - now);
    if (remaining <= 0) return;

    struct DeadlineTimer {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
        DeadlineTimer() noexcept {
            if (!handle) handle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
        ~DeadlineTimer() { if (handle) CloseHandle(handle); }
    };
    static DeadlineTimer timer;
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(remaining) * 10000;
    const bool armed = timer.handle && SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE);
    CorePerf::Scope coreWait(CorePerf::IdleWait);
    // INPUTAVAILABLE also wakes for input already inspected by PeekMessage;
    // no message is removed or dispatched here. The normal pump retains order.
    MsgWaitForMultipleObjectsEx(armed ? 1 : 0, armed ? &timer.handle : nullptr,
        armed ? INFINITE : static_cast<DWORD>(remaining), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (armed) CancelWaitableTimer(timer.handle);
}
static bool NGame::ProcessMessages()
{
    MSG msg;
    bool bActive = true;
    // Decode advancement and rendering stay on the host thread, including
    // intervals between loading steps when the host pumps messages/idle.
    C2DScreenWait::Update();
    // Idle processing until no new message come
    while (!PeekMessage(&msg, 0, 0, 0,
                        PM_NOREMOVE)) // no messages in the queue => process OnIdle
    {
        CorePerf::Frame coreFrame;
        GameJournal::MemoryFrame(GetTickCount());
        C2DScreenWait::Update();
        // Update render state
        UpdateRenderEnable();
        bool diagnosticFrameOwner = FrameDiagnostics::Begin(ModShadowSchedule::generationTicks);
        {
            FrameDiagnostics::Scope timing(FrameDiagnostics::Simulation);
            bActive = CountTime();
        }
        if (bActive)
        {
            {
                FrameDiagnostics::Scope timing(FrameDiagnostics::Idle);
                CorePerf::Scope coreTiming(CorePerf::OnIdle);
                theInterface.OnIdle();
            }
            NGame::DrawFrame(bEnableFlip);
        }
        if (diagnosticFrameOwner)
        {
            const CPlot3D &anchor = theCamera.GetAnchorPoint();
            FrameDiagnostics::End(ModShadowSchedule::generationTicks, anchor.x, anchor.y);
        }
        LiveObserver::Frame();
        WaitForIdleWork();
    }
    CorePerf::Scope coreMessages(CorePerf::Messages);
    if (!GetMessage(&msg, 0, 0, 0))
        return false;
    FrameDiagnostics::Message();
    TranslateMessage(&msg);
    // DispatchMessage( &msg );
    if (bActive)
        NGame::WndProc(msg.message, msg.wParam, msg.lParam);
    else
        NGame::ShortWndProc(NGame::hWnd, msg.message, msg.wParam, msg.lParam);
    coreMessages.Finish();
    CorePerf::Flush();
    return true;
}

const int BAD_RESULT = 0xDEAD;

// The renderer reads game archives (MOB sources, models) for the whole session,
// not only during WinMainInit: its resource lease lives until shutdown.
static std::unique_ptr<OwnedRender::ResourceRuntimeLease> ownedResourceRuntime;
int WinMainInit(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    CStringArray szParams;
    CStringArray szSysMsgs;
    // load system messages
    {
        FILE *file = fopen(".\\res\\sysmsg.txt", "rt");
        if (file == 0)
        {
            szSysMsgs.Add("This Game requires DX7 in order to run!");
            szSysMsgs.Add("Not enough free disk space - play on your own risk!");
            szSysMsgs.Add("Please, insert EI disc 2 in the CD-ROM and press OK");
        }
        else
        {
            int nLen = _filelength(_fileno(file));
            char *pBuffer = dbgnew char[nLen];
            memset(pBuffer, 0, nLen);
            fread(pBuffer, 1, nLen, file);
            fclose(file);
            file = 0;
            //
            NStrProc::SplitString(pBuffer, szSysMsgs, 10);
            if (szSysMsgs.GetSize() < 3)
            {
                szSysMsgs.RemoveAll();
                szSysMsgs.Add("This Game requires DX7 in order to run!");
                szSysMsgs.Add("Not enough free disk space - play on your own risk!");
                szSysMsgs.Add("Please, insert EI disc 2 in the CD-ROM and press OK");
            }
            //
            delete[] pBuffer;
        }
    }
    //

#ifdef FINALRELEASE
    DWORD dwDXVersion = GetDXVersion();
    if (dwDXVersion < 0x700)
    {
        ErrorDialog::ShowA(nullptr, szSysMsgs[0], "CRITICAL ERROR", MB_OK | MB_ICONEXCLAMATION);
        return 0xDEAD;
    }
#endif // FINALRELEASE
    //
    // register classes for save/load
    REGISTER_SAVELOAD_CLASS(1, CShop);
    REGISTER_SAVELOAD_CLASS(2, CSingleParty);

    // split command line
    NStrProc::SplitString(lpCmdLine, szParams, ' ');
    // process command line
    NGame::szCameraFileName = ".\\camera.cam";
    NGame::szScriptFileName.Empty();
    NGame::szObjectsFileName.Empty();
    NGame::szMapFileName.Empty();
    for (int i = 0; i < szParams.GetSize(); i++)
    {
        if (szParams[i].Find("-moviepath_") == 0)
        {
            theWorld.ResetFlag(WC_MAKE_MOVIE);
            NGame::szMovieDir = szParams[i].Right(szParams[i].GetLength() - 11);
            if (NGame::szMovieDir[NGame::szMovieDir.GetLength() - 1] != '\\')
                NGame::szMovieDir += '\\';
        }
        else if (!theWorld.CheckFlag(WC_SAVE_CAMERA) && (szParams[i].CompareNoCase("-nocam") == 0))
            theWorld.ResetFlag(WC_USE_CAMERA_SCRIPT);
        else if (!theWorld.CheckFlag(WC_SAVE_CAMERA) && (szParams[i].Find(".cam") != -1))
            NGame::szCameraFileName = szParams[i];
        else if (szParams[i].Find(".scr") != -1)
            NGame::szScriptFileName = szParams[i];
        else if (szParams[i].Find(".mob") != -1)
            NGame::szObjectsFileName = szParams[i];
        else if (szParams[i].Find(".mpr") != -1)
            NGame::szMapFileName = szParams[i];
        else if (szParams[i].Find("-benchmark_") == 0)
        {
            theSettings.nNumberOfFrames = atoi(szParams[i].Right(szParams[i].GetLength() - 11));
            theSettings.nCreateOutputFile = 1;
        }
        else if (szParams[i].Find("-devicetarget_") == 0)
            theSettings.nDrawingTarget = atoi(szParams[i].Right(szParams[i].GetLength() - 14));
    }
    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, NGame::szAppTitleName, NGame::MAX_LOADSTRING);
    LoadString(hInstance, IDS_WND_CLASS, NGame::szWndClassName, NGame::MAX_LOADSTRING);
    // create and register class style
    NGame::atomWndClassName = NGame::MyRegisterClass(hInstance);
    // check for this app already runed
    if (!NGame::CheckPreviousApp(reinterpret_cast<LPCSTR>(NGame::atomWndClassName),
                                 NGame::szAppTitleName))
        exit(-1);
    // setup registry key
    CWorld::SetRegistryKey("Software\\Gipat.ru\\EI_Starter\\EvilIslands");
    CWorld::InitCurrentDir();
    theSettings.ReadSettingsFromRegistry(CWorld::GetRegistryKey());
#if !defined(FINALRELEASE) || defined(USE_SETTINGS_INI) && !defined(DONT_USE_SETTINGS_INI)
    theSettings.ReadSettingsFromFile(RuntimePaths64::ConfigRead("settings.ini", theSettings.szWorkPath + "config\\settings.ini").c_str());
#endif // !defined(FINALRELEASE) || defined(USE_SETTINGS_INI) && !defined(DONT_USE_SETTINGS_INI)
       //
       //
       // set new memory depleted handler
       // create worlds and establish connection to local server
    // Game data is rooted at WorkPath, independently of the launcher CWD.
    // Writable settings/logs remain owned by RuntimePaths64.
    CWorld::InitCurrentDir(theSettings.szWorkPath);
    theAIGeneralSettings.ReadFromFile(CWorld::GetCurrentPath() + "config\\ai.reg");
    if (AgentBackground::Enabled()) theSettings.nFullScreen = 0; // transient, never saved
    OwnedRender::ConfigureStartup();
    ownedResourceRuntime = std::make_unique<OwnedRender::ResourceRuntimeLease>();
    theWorld.Create();
    theWorldServer.Create();
    theCamera.Create(&theSettings, theSettings.fpNearClipDistance, theSettings.fpFarClipDistance,
                     static_cast<float>(pi / 3.5));
    theWorld.SetCamera(&theCamera);
    //
    // CD-2 check
    {
        CString szCDPath = theWorld.GetCDPath();
        CString szWorkPath = theWorld.GetWorkPath();
        // check disk free space
        char drive[128], dir[1024], fname[128], ext[128];
        _splitpath(szWorkPath, drive, dir, fname, ext);
        strcat(drive, "\\.");
        double fDiskFreeSpace = GetFreeDiskSpace(drive);
        if (fDiskFreeSpace != 0)
        {
            if (fDiskFreeSpace < 300.0 * double(1L << 20))
            {
                int nRetCode = ErrorDialog::ShowA(nullptr, szSysMsgs[1], "Warning",
                                            MB_OKCANCEL | MB_ICONWARNING);
                if (nRetCode == IDCANCEL)
                    return 0xDEAD;
            }
        }
        // check CD2 in CD-ROM for non-full install
        if (szWorkPath != szCDPath) // non-full install
        {
            char name[1024];
            memset(name, 0, sizeof(name));
            // retrieve CD volume information
            _splitpath(szCDPath, drive, dir, fname, ext);
            strcat(drive, "\\.");
            GetVolumeInformation(drive, name, 1024, 0, 0, 0, 0, 0);
            while (theSettings.szCD2Label != name)
            {
                int nRetCode = ErrorDialog::ShowA(nullptr, szSysMsgs[2], "Warning",
                                            MB_OKCANCEL | MB_ICONWARNING);
                if (nRetCode == IDCANCEL)
                    return BAD_RESULT;
                GetVolumeInformation(drive, name, 1024, 0, 0, 0, 0, 0);
            }
        }
    }

    //
    CString szPath;
    // create textures directory
    szPath.Format("%s", RuntimePaths64::Textures().c_str());
    CreateDirectory(szPath, 0);
    // Perform application initialization
    if (!NGame::InitInstance(hInstance, nCmdShow))
        return BAD_RESULT;

    // setup camera placement
    {
        CPlot3D cameraCP;
        cameraCP.x = theSettings.fpCameraDefaultXDeploy;
        cameraCP.y = theSettings.fpCameraDefaultYDeploy;
        cameraCP.z = theSettings
                         .fpCameraDefaultHeight; // + theWorld.GetZValue( cameraCP.x, cameraCP.y );
        theCamera.SetCP(cameraCP);
    }

    // create CRenderer object and CDXGeometryProcessor object in visualizator
    GetRenderer()->Create(NGame::hWnd, theSettings);
    // initialize sound manager, if audio enabled
    if (theSettings.nAudio)
    {
        CString szResourcesPath = theWorld.GetResourcePath();
        CString szWorkPath = theWorld.GetWorkPath();
        CString szCDPath = theWorld.GetCDPath();
        // create all data structures for sound manager
        theWorld.GetSoundManager()->Create(NGame::hWnd, TRUE);
        // select resource for streaming sound
        theWorld.GetSoundManager()->SelectStreamingResource(szWorkPath + "stream\\",
                                                            szResourcesPath + "streamsn.reg");
        // select resource for SFXes
        theWorld.GetSoundManager()->SelectFXResource(szResourcesPath + "sfx.res");
        {
            CString szSpeechRes = szCDPath + "res\\speech.res";
#ifdef __DEMO__
            CFileFind find;
            if (find.FindFile(szSpeechRes) != FALSE)
                theWorld.GetSoundManager()->AddFXResource(szCDPath + "res\\speech.res");
            find.Close();
#else
            theWorld.GetSoundManager()->AddFXResource(szCDPath + "res\\speech.res");
#endif // __DEMO__
        }
        // set sound volumes
        theWorld.GetSoundManager()->SetFXMasterVolume(theSettings.nAudioSFX);
        theWorld.GetSoundManager()->SetSpeechMasterVolume(theSettings.nAudioSpeech);
        theWorld.GetSoundManager()->SetMusicMasterVolume(theSettings.nAudioStreaming);
        theWorld.GetSoundManager()->SetStereoReverse(theSettings.nReverseStereo ? true : false);
    }

    {
        // play start movie sequence
        NGame::PlayMoviesSequence("Start");
        // play static logo
        C2DScreenWait::pszProgressName = "Progres.bik";
        // The wait screen stays up through the database and main-menu zone
        // loads; CWorldClient::NewZone clears it when the menu zone is ready.
        C2DScreenWait::Step(0);
    }
#if !defined(NODEBUGINFO) && !defined(_DEBUG)
    // EngineDiagnostics owns the outer exception boundary.
#endif // !defined( NODEBUGINFO ) && !defined( _DEBUG )
       // load databases
    thePerkDatabase.LoadDatabase(theSettings.szResourcePath, "database.res");
    theQuestDatabase.Load(theSettings.szResourcePath);
    theUnitDatabase.Load(theSettings.szResourcePath, "database.res");
    theSpellDatabase.Load(theSettings.szResourcePath, "database.res");
    theItemDatabase.Load(theSettings.szResourcePath, "database.res");
    theLeverDatabase.Load(theSettings.szResourcePath);
    theAckDatabase.Load(theSettings.szResourcePath);
    theTPDatabase.Load(theSettings.szResourcePath);
    GameJournal::Event("SESSION", "settings", "resource_path=%s mod=%s width=%lu height=%lu language=unknown system_ui_lang=%u", GameJournal::Quote(theSettings.szResourcePath).c_str(), GameJournal::Quote(ModRuntime::Resources() ? ModRuntime::Resources()->Directory().c_str() : "stock").c_str(), theSettings.dwWidth, theSettings.dwHeight, unsigned(GetUserDefaultUILanguage()));
    // final world initialization
    theWorld.Init(&theRenderer);
    TheCursorInit();
    theInterface.Init(&theRenderer);
    theMap.Init();
    // go to game
    LoadGame();
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////
int RunGame(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow,
            std::unique_ptr<ModRuntime::Host> modHost = nullptr, bool legacyRun = false)
{
    if(strcmp(lpCmdLine,"--inspect-starter-selection")==0){
        try { auto host=ModRuntime::Host::FromCommandLine();return host&&ModRuntime::Resources()?0:BAD_RESULT; }
        catch(const std::exception&){ GameJournal::AllocationFailureInCatch("GameMain");return BAD_RESULT;}
    }
    int inspectionResult=0;
    if(InspectSettingsCommandLine(inspectionResult))return inspectionResult;
    if(InspectItemUiCommandLine(inspectionResult))return inspectionResult;
    if(InspectHitHistoryCommandLine(inspectionResult))return inspectionResult;
    if(InspectNpcEquipmentCommandLine(inspectionResult))return inspectionResult;
    if(InspectStealingPolicyCommandLine(inspectionResult))return inspectionResult;
    if(InspectItemSpellPolicyCommandLine(inspectionResult))return inspectionResult;
    if(InspectMobFlagArgumentsCommandLine(inspectionResult))return inspectionResult;
    if(InspectNativeRandomCommandLine(inspectionResult))return inspectionResult;
    if(InspectModConfigurationCommandLine(inspectionResult))return inspectionResult;
    if(InspectWorldBackgroundCommandLine(inspectionResult))return inspectionResult;
    if(InspectModPackageCommandLine(inspectionResult))return inspectionResult;
    CorePerf::Initialize();
    GameJournal::Start();
    if (AgentBackground::Enabled())
        GameJournal::Event("SESSION", "mcp_background", "argument=--background windowed=1 no_activate=1 audio=muted settings=read_only saves=mcp_");
    LiveObserver::Start();
#if defined(__SANITIZE_ADDRESS__)
    ASanReporting64::JournalReady();
#endif
    struct JournalEnd { ~JournalEnd() {
#if defined(__SANITIZE_ADDRESS__)
        ASanReporting64::Finish();
#endif
        LiveObserver::Stop(); CorePerf::Flush(); GameJournal::Stop(); } } journalEnd;
    try { SettingsProfile::Initialize();
        GameJournal::Event("SESSION","settings_source","%s",SettingsProfile::Summary().c_str());
        if (!legacyRun) modHost=ModRuntime::Host::FromCommandLine(); }
    catch(const std::exception&e){ GameJournal::AllocationFailureInCatch("GameMain");EngineDiagnostics::Caught(e.what());return BAD_RESULT;}
    if (!ModRuntime::Current())
    {
        GameJournal::Event("SESSION", "mod_required", "EIRebuild requires a loaded mod");
        if (AgentBackground::Enabled()) { AgentBackground::DialogError("EIRebuild requires a loaded mod", "EIRebuild"); return BAD_RESULT; }
        MessageBoxW(nullptr, L"EIRebuild: \u043d\u0435 \u0437\u0430\u0433\u0440\u0443\u0436\u0435\u043d \u043c\u043e\u0434. \u0412\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u043c\u043e\u0434 \u0432 ReStarter \u0438 \u043f\u043e\u0432\u0442\u043e\u0440\u0438\u0442\u0435 \u0437\u0430\u043f\u0443\u0441\u043a.",
                    L"EIRebuild", MB_OK | MB_ICONERROR);
        return BAD_RESULT;
    }
    // Also covers failed startup/load and exception exits before normal FINI.
    struct MovieLifetime {
        bool active = true;
        void Finish() {
            if (!active) return;
            C2DScreenWait::Clear();
            Video::StopMovies();
            Video::SetMovieRenderer(nullptr);
            active = false;
        }
        ~MovieLifetime() { Finish(); }
    } movieLifetime;
    OutputDebugStringA("Hello!\n");
    try
    {
        if (WinMainInit(hInstance, hPrevInstance, lpCmdLine, nCmdShow) == BAD_RESULT)
            return BAD_RESULT;
    }
    catch (const std::exception& e)
    { GameJournal::AllocationFailureInCatch("GameMain");
        EngineDiagnostics::Caught(e.what());
        return BAD_RESULT;
    }

        //
        // Main message loop:
#if defined(FINALRELEASE) && defined(NODEBUGINFO)
    try
#endif // defined( FINALRELEASE ) && defined( NODEBUGINFO )
    {
        while (true)
        {
            if (!NGame::ProcessMessages())
                break;
        }
    }
#if defined(FINALRELEASE) && defined(NODEBUGINFO)
    catch (...)
    { GameJournal::AllocationFailureInCatch("GameMain");
        EngineDiagnostics::Caught("Unhandled game-loop exception");
        return BAD_RESULT;
    }
#endif // defined( FINALRELEASE ) && defined( NODEBUGINFO )
    //
#if !defined(NODEBUGINFO) && !defined(_DEBUG)
    // EngineDiagnostics owns the outer exception boundary.
#endif // !defined( NODEBUGINFO ) && !defined( _DEBUG )

    movieLifetime.Finish();
    FrameDiagnostics::Flush();
#if defined(__SANITIZE_ADDRESS__)
    ASanReporting64::Finish();
#endif
    GameJournal::Stop();
    // FINI
    theWorld.GetTextureManager()->DeleteUnitLayerTextures();
    theInterface.Fini();
    theCursor.Fini();
    // GPU-backed world resources are released before the renderer session.
    //
    theSettings.WriteSettingsToRegistry(theWorld.GetRegistryKey());
#if !defined(FINALRELEASE) || defined(USE_SETTINGS_INI) && !defined(DONT_USE_SETTINGS_INI)
    theSettings.WriteSettingsToFile(RuntimePaths64::Config("settings.ini").string().c_str());
#endif // !defined(FINALRELEASE) || defined(USE_SETTINGS_INI) && !defined(DONT_USE_SETTINGS_INI)
       //
    theWorld.SetCamera(0);
    // The renderer and its TL pipeline hold references to the sunlight owned
    // by the world's light manager: drop them while that light still exists.
    GetRenderer()->SetSunlight(nullptr);
    SaveSchema::Slot::Shutdown();
    theWorld.Destroy();
    GetRenderer()->Destroy();
    ownedResourceRuntime.reset();

    DumpUnfreedMemory(AMTS_BYFILENAME);
    //_CrtDumpMemoryLeaks();
    // return msg.wParam;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
//  FUNCTION: WndProc( HWND, unsigned, WORD, LONG )
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_DESTROY	- post a quit message and return
LRESULT NGame::WndProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ErrorDialog::HandleOwnerMessage(message, wParam)) return 1;
    if (OwnedRender::E2BlockBackgroundInput(hWnd, message, wParam, lParam)) return 1;
    theInterface.CheckScreensMustDie();
    // c56 focus/editor gate wraps overlay dispatch; both camp consumers see
    // original mouse coordinates, before the single native remap.
    OwnedRender::E3RefreshInterface();
    if (OwnedRender::E3BlockInput(message, wParam))
        return SendMessage(hWnd, message, wParam, lParam);
    if (message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN)
        LiveObserver::ReleaseUiCursor();
    E4CampAccess::BeginInput();
    if (OwnedRender::E2CampInput(message, wParam, lParam)) return 1;
    if (OwnedRender::E4::Message(NGame::hWnd, message, wParam, lParam)) return 1;
    lParam = owned::game_native_mouse_message(message, lParam, false);
    switch (message)
    {
    case WM_SYSKEYDOWN:
        if (wParam == VK_F4 && (lParam & 0x20000000))
            NGame::QuitProgram(0);
        else
            theInterface.OnKeyDown((UINT)wParam, lParam & 0xFFFF, lParam >> 16);
        break;
    case WM_SYSKEYUP:
        NConsole::game << wParam << ':' << lParam << endl;
        if (wParam == VK_TAB && (lParam & 0x20000000))
            ShortWndProc(NGame::hWnd, WM_ACTIVATE, (IsIconic(NGame::hWnd) << 16) | WA_INACTIVE, 0);
        else
            theInterface.OnKeyUp((UINT)wParam, lParam & 0xFFFF, lParam >> 16);
        break;
    case WM_KEYDOWN:
        theInterface.OnKeyDown((UINT)wParam, lParam & 0xFFFF, lParam >> 16);
        break;
    case WM_KEYUP:
        theInterface.OnKeyUp((UINT)wParam, lParam & 0xFFFF, lParam >> 16);
        break;
    case WM_CHAR:
        theInterface.OnChar((UINT)wParam, lParam & 0xFFFF, lParam >> 16);
        break;

    case WM_MOUSEMOVE:
        theInterface.OnMouseMove(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    case WM_MOUSEWHEEL:
        theInterface.OnMouseWheel(LOWORD(wParam), (short)HIWORD(wParam),
                                  CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;

    // CRAP{ noone knows why Genius mouse driver sends this message instead of WM_MOUSEWHEEL =
    // 0x020A
    case 0xCC54:
        theInterface.OnMouseWheel(0, (short)(signed)(wParam),
                                  CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
        // CRAP} parameters are also different from default

    case WM_LBUTTONDOWN:
        theInterface.OnLButtonDown(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    case WM_LBUTTONUP:
        theInterface.OnLButtonUp(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    case WM_LBUTTONDBLCLK:
        theInterface.OnLButtonDblClk(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    case WM_RBUTTONDOWN:
        theInterface.OnRButtonDown(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    case WM_RBUTTONUP:
        theInterface.OnRButtonUp(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    case WM_RBUTTONDBLCLK:
        theInterface.OnRButtonDblClk(wParam, CPoint(short(LOWORD(lParam)), short(HIWORD(lParam))));
        break;
    default:
        // return DefWindowProc( hWnd, message, wParam, lParam );
        return ShortWndProc(hWnd, message, wParam, lParam);
        break;
        //
    }
    theInterface.CheckScreensMustDie();
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK NGame::ShortWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (AgentBackground::Enabled()) {
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATEANDEAT;
        if (message == WM_WINDOWPOSCHANGING && lParam) {
            auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
            position->flags |= SWP_NOACTIVATE;
            if (!(position->flags & SWP_NOZORDER)) position->hwndInsertAfter = HWND_BOTTOM;
        }
        if (message == WM_ACTIVATE || message == WM_ACTIVATEAPP || message == WM_SETFOCUS || message == WM_KILLFOCUS) {
            bEnableRender = true; bEnableRenderStateChanged = true;
            theInterface.ResetInputState();
            return 1;
        }
    }
    if (message == WM_KILLFOCUS || message == WM_SETFOCUS || message == WM_ACTIVATE || message == WM_ACTIVATEAPP ||
        message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_SIZE || message == WM_DESTROY || message == WM_NCDESTROY)
        owned::camp_inventory_input(hWnd, message, wParam, lParam, false);
    if (ErrorDialog::HandleOwnerMessage(message, wParam)) return 1;
    OwnedRender::E3RefreshInterface();
    if (owned::game_focus_swallow_activation(hWnd, message, wParam)) return 1;
    WORD wID;

    switch (message)
    {
    case WM_PAINT:
        // DefWindowProc( NGame::hWnd, message, wParam, lParam );
        PAINTSTRUCT ps;
        RECT rect;
        if (GetUpdateRect(hWnd, &rect, FALSE))
        {
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_ACTIVATE:
        if (OwnedRender::E2BackgroundConfigured())
        {
            OwnedRender::E2Clock(timeGetTime(), OwnedRender::E2BackgroundPaused(hWnd));
            theInterface.ResetInputState();
            break;
        }
        if (!(HIWORD(wParam))) // if window is not minimized
        {
            CSoundManager *pSoundManager = theWorld.GetSoundManager();
            switch (LOWORD(wParam))
            {
            case WA_CLICKACTIVE:
            case WA_ACTIVE:
                bEnableRender = true;
                bEnableRenderStateChanged = true;
                ShowWindow(NGame::hWnd, SW_RESTORE);
                theCursor.Show(true);
                if (pSoundManager)
                    pSoundManager->UnpauseAllSounds();
                theInterface.ResetInputState();
                break;
            case WA_INACTIVE:
                bEnableRender = false;
                bEnableRenderStateChanged = true;
                theCursor.Show(false);
                if (pSoundManager)
                    pSoundManager->PauseAllSounds();
                ShowWindow(NGame::hWnd, SW_MINIMIZE);
                break;
            }
        }
        else
        {
            bEnableRender = false;
            bEnableRenderStateChanged = true;
        }
        break;
    case WM_DESTROY:
        NGame::QuitProgram(0);
        break;
    case WM_SYSCOMMAND:
        wID = wParam & 0xFFF0;
        switch (wID)
        {
        case SC_SCREENSAVE:
        case SC_MONITORPOWER:
            // ???
            return 0;
        case SC_NEXTWINDOW:
        case SC_PREVWINDOW:
        case SC_TASKLIST:
            NConsole::game << "switch task" << endl;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_SETCURSOR:
        if (!owned::apply_load_cursor()) SetCursor(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void NGame::DrawFrame(bool bFlip, bool bDraw2D)
{
    CorePerf::Scope coreTiming(CorePerf::DrawFrame);
    if (OwnedRender::E2BackgroundPaused(hWnd)) { CorePerf::DrawSkipped(0); Sleep(50); return; }
    OwnedRender::E2PassScope pass(OwnedRender::Pass::frame);
    OwnedRender::E1BeginFrame();
    OwnedRender::E1TextBeginFrame();
    if (!bEnableRender) { CorePerf::DrawSkipped(1); return; }
    if (!bNeedRender) { CorePerf::DrawSkipped(2); return; }
    if (!theInterface.GetCurrentScreen())
    {
        CorePerf::DrawSkipped(3);
        Sleep(50);
        return;
    }

    auto pDX11 = theRenderer.GetD3D();
    CorePerf::Scope coreLock(CorePerf::LockFrameWait);
    const auto renderAccess = pDX11->GetOwnedRenderer().LockFrame();
    coreLock.Finish();

    if (theRenderer.BeginDrawFrame())
    {
        pDX11->BeginFrame();
        pDX11->GetSpriteBatch()->Begin(DirectX::SpriteSortMode_Immediate);
        {
            CorePerf::Scope coreTiming(CorePerf::Draw3D);
            CorePerf::VisualFrame coreVisual;
            FrameDiagnostics::Scope timing(FrameDiagnostics::Draw3D);
            theInterface.Draw();
        }
        pDX11->GetSpriteBatch()->End();
        theRenderer.EndDrawFrame();
        if (bDraw2D)
        {
            CorePerf::Scope coreTiming(CorePerf::Draw2D);
            FrameDiagnostics::Scope timing(FrameDiagnostics::Draw2D);
            OwnedRender::E2PassScope uiPass(OwnedRender::Pass::ui);
            theInterface.Draw2D();
        }

        const bool present = bFlip && bEnableRender && bNeedRender;
        if (present)
            theCursor.Draw();
        pDX11->EndFrame(present);
    }
    else CorePerf::DrawSkipped(4);
}
/////////////////////////////////////////////////////////////////////////////////////
void NGame::DestroyInputMessages()
{
    MSG msg;
    // destroy all messages in the queue
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        NGame::ShortWndProc(NGame::hWnd, msg.message, msg.wParam, msg.lParam);
}
/////////////////////////////////////////////////////////////////////////////////////
void NGame::PlayMoviesSequence(LPCSTR pszSection, bool bInitialClear,
                               bool bClearSurfaceBetweenMovies, bool bStopAllSounds,
                               bool bUseOverlays)
{
    CStringArray szNames;
    CIniFile reg;
    const CString fallback = theWorld.GetWorkPath() + "config\\movie.ini";
    const std::string movieConfig = ModRuntime::ConfigPath((LPCSTR)fallback);
    reg.Open(movieConfig.c_str());
    reg.GetStringArray(pszSection, "movies", szNames);

    for (int i = 0; i < szNames.GetSize(); ++i)
    {
        Video::Movie::Play(theWorld.GetCDPath() + "Movies\\" + szNames[i], 0, true, false,
                         bInitialClear || bClearSurfaceBetweenMovies, bStopAllSounds, bUseOverlays);
    }
}
/////////////////////////////////////////////////////////////////////////////////////

// Keep C++ ownership in a callee, outside the SEH frame's local variables.
static int RunLegacyEntry(HINSTANCE instance, HINSTANCE previous, int show)
{
    std::unique_ptr<ModRuntime::Host> host;
    bool run = false;
    const int result = ModCli::Dispatch(host, run);
    if (!run) return result;
    EngineDiagnostics::Initialize(false);
    char emptyCommand[] = "";
    return RunGame(instance, previous, emptyCommand, show, std::move(host), true);
}

static int GuardLegacyEntry(HINSTANCE instance, HINSTANCE previous, int show)
{
    __try { return RunLegacyEntry(instance, previous, show); }
    __except (EngineDiagnostics::Filter(GetExceptionInformation()))
    {
        TerminateProcess(GetCurrentProcess(), 0xE0000135);
        return BAD_RESULT;
    }
}

static int RunMainEntry(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show, bool probe)
{
    if (probe)
    {
        EngineDiagnostics::Note(command);
        if (strcmp(command + 23, "cpp") == 0) throw std::runtime_error("diagnostic probe");
        if (strcmp(command + 23, "crt") == 0) _invalid_parameter_noinfo();
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }
    EngineDiagnostics::Note("Entering game startup");
    return RunGame(instance, previous, command, show);
}

bool InspectRendererFacadeCheckCommandLine(int& result);

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
#if defined(__SANITIZE_ADDRESS__)
    ASanReporting64::Initialize();
#endif
    int profileResult=0;
    if (InspectRendererFacadeCheckCommandLine(profileResult)) return profileResult;
    if(InspectSettingsProfileCommandLine(profileResult))return profileResult;
    const int cliResult = GuardLegacyEntry(instance, previous, show);
    if (cliResult != ModCli::NotCli) return cliResult;
    int diagnosticResult = 0;
    if (InspectDiagnosticsCommandLine(diagnosticResult)) return diagnosticResult;
    const bool probe = strncmp(command, "--diagnostics-selftest=", 23) == 0;
    EngineDiagnostics::Initialize(probe);
    __try
    {
        return RunMainEntry(instance, previous, command, show, probe);
    }
    __except (EngineDiagnostics::Filter(GetExceptionInformation()))
    {
        TerminateProcess(GetCurrentProcess(), 0xE0000135);
        return BAD_RESULT;
    }
}
