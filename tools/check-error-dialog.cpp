#include <windows.h>
static bool emulateForeground;
static HWND TestForeground()
{
    return emulateForeground ? GetActiveWindow() : GetForegroundWindow();
}
// The private desktop cannot own the input desktop's foreground. A second run
// substitutes only that query to exercise continuation/restore with real USER32.
#define GetForegroundWindow TestForeground
#include "../Game/ErrorDialog.h"
#undef GetForegroundWindow
#include <cstdio>
#include <string>

// Native window/message-loop test on an isolated desktop. No game or renderer.
static HWND owner;
static int mode, failed, observed;
static bool legacy;
static HWND seen;
static RECT before;
static bool clipSupported;
static bool foregroundSupported;
static void Check(bool ok, const char *what)
{
    if (!ok) { ++failed; std::printf("FAIL %s mode=%d\n", what, mode); }
}
static LRESULT CALLBACK Window(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    // Emulate an existing input owner swallowing keyboard/close requests.
    // The production scope's last-installed subclass must run before this.
    if (!legacy && (message == WM_CLOSE || message == WM_SYSKEYDOWN)) return 1;
    if (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && !HIWORD(wp))
        ShowWindow(window, SW_MINIMIZE); // Exact legacy activation action.
    if (message == WM_TIMER && seen)
    {
        std::printf("TIMER mode=%d\n", mode);
        char title[128]{}; GetWindowTextA(seen, title, sizeof(title));
        std::printf("DIALOG text=%s valid=%d\n", title, IsWindow(seen));
        EnumChildWindows(seen, [](HWND w, LPARAM) -> BOOL {
            char cls[32]{}, text[80]{}; GetClassNameA(w,cls,sizeof(cls)); GetWindowTextA(w,text,sizeof(text));
            std::printf("CONTROL class=%s id=%d text=%s\n", cls, GetDlgCtrlID(w), text); return TRUE;
        }, 0);
        KillTimer(window, 1);
        if (!legacy)
        {
            Check(ErrorDialog::Active(), "active scope");
            Check(!IsIconic(owner), "owner not minimised");
            Check(IsWindowEnabled(owner), "owner close button enabled");
            Check(GetCapture() == nullptr, "capture released during dialog");
            if (foregroundSupported) Check(TestForeground() == seen, "dialog foreground");
            if (foregroundSupported) Check(GetFocus() == seen || IsChild(seen, GetFocus()), "dialog focus");
            Check(GetWindow(seen, GW_OWNER) == owner, "dialog ownership");
            RECT clip{}; GetClipCursor(&clip);
            if (clipSupported) Check(!EqualRect(&clip, &before), "clip released during dialog");
            ++observed;
        }
        else std::printf("LEGACY owner_minimised=%d owner_enabled=%d capture_held=%d\n",
            IsIconic(owner), IsWindowEnabled(owner), GetCapture() == owner);
        if (mode == 1) SendMessage(owner, WM_CLOSE, 0, 0);
        else if (mode == 2) SendMessage(owner, WM_SYSKEYDOWN, VK_F4, 1 << 29);
        else if (mode == 3) SendMessage(seen, WM_CLOSE, 0, 0);
        else
        {
            SetActiveWindow(seen);
            HWND button = GetDlgItem(seen, IDOK);
            // Windows uses IDCANCEL for the sole OK button of MB_OK.
            if (!button) button = GetDlgItem(seen, IDCANCEL);
            std::printf("BUTTON exists=%d enabled=%d active=%d\n", button != nullptr, IsWindowEnabled(button), GetActiveWindow() == seen);
            SendMessage(button, BM_CLICK, 0, 0);
            Check(button != nullptr, "real OK button present");
        }
        std::printf("DISMISS mode=%d\n", mode);
        return 0;
    }
    return DefWindowProc(window, message, wp, lp);
}
static LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_CREATEWND && reinterpret_cast<HWND>(wp) != owner)
    {
        char name[32]{}; GetClassNameA(reinterpret_cast<HWND>(wp), name, sizeof(name));
        std::printf("CREATE class=%s\n", name);
        if (lstrcmpA(name, "#32770") == 0)
        { seen = reinterpret_cast<HWND>(wp); SetTimer(owner, 1, 100, nullptr); }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
static int Child()
{
    FILE *log = nullptr; freopen_s(&log, emulateForeground ? "dialog-restore.log" : "dialog-child.log", "wt", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("CHILD started\n");
    WNDCLASSA cls{}; cls.lpfnWndProc = Window; cls.lpszClassName = "R43ErrorOwner";
    cls.hInstance = GetModuleHandle(nullptr); RegisterClassA(&cls);
    owner = CreateWindowA(cls.lpszClassName, "R43 synthetic owner", WS_OVERLAPPEDWINDOW,
        50, 50, 800, 600, nullptr, nullptr, cls.hInstance, nullptr);
    Check(owner != nullptr, "synthetic owner created");
    HHOOK hook = SetWindowsHookEx(WH_CBT, Observe, nullptr, GetCurrentThreadId());
    for (mode = 0; mode != 5; ++mode)
    {
        legacy = mode == 4; seen = nullptr;
        std::printf("MODE %d\n", mode);
        ShowWindow(owner, SW_RESTORE); SetForegroundWindow(owner); SetFocus(owner);
        foregroundSupported = TestForeground() == owner;
        SetCapture(owner);
        before = {100, 100, 300, 300}; clipSupported = ClipCursor(&before) != FALSE;
        if (legacy) MessageBoxA(owner, "Legacy activation reproduction", "R43", MB_OK | MB_ICONERROR);
        else if (mode == 3) ErrorDialog::ShowW(owner, L"Fatal wide path", L"R43", MB_OK | MB_ICONERROR, false);
        else ErrorDialog::ShowA(owner, "Production error dialog", "R43", MB_OK | MB_ICONERROR);
        if (!legacy)
        {
            Check(!ErrorDialog::Active(), "scope cleared");
            MSG msg{}; bool quit = PeekMessage(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE;
            Check(quit == (mode == 1 || mode == 2), "owner close posts quit despite modal loop");
            if (mode == 0 && foregroundSupported)
            {
                Check(GetCapture() == owner, "capture restored on continuation");
                Check(TestForeground() == owner, "foreground restored on continuation");
                RECT clip{}; GetClipCursor(&clip);
                if (clipSupported) Check(EqualRect(&clip, &before), "clip restored on continuation");
            }
            else Check(GetCapture() == nullptr, "no capture restored during exit");
        }
        ReleaseCapture(); ClipCursor(nullptr);
    }
    UnhookWindowsHookEx(hook); DestroyWindow(owner);
    std::printf("DIALOG_SUMMARY observed=%d failures=%d clip_supported=%d foreground_supported=%d simulated_foreground=%d private_desktop=1\n", observed, failed, clipSupported, foregroundSupported, emulateForeground);
    return failed || observed != 4 ? 1 : 0;
}
int main(int argc, char **)
{
    if (argc > 1) { emulateForeground = argc > 2; return Child(); }
    wchar_t name[80]{}; swprintf_s(name, L"R43-dialog-%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) return 2;
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(path) + L"\" child";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = name;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path, &command[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process))
    { CloseDesktop(desktop); return 3; }
    DWORD result = 4;
    if (WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &result);
    else TerminateProcess(process.hProcess, 4);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (result == 0)
    {
        command += L" emulate";
        if (!CreateProcessW(path, &command[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) result = 3;
        else
        {
            if (WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &result);
            else { TerminateProcess(process.hProcess, 4); result = 4; }
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
    }
    CloseDesktop(desktop);
    return static_cast<int>(result);
}
