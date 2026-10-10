#pragma once
#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

// The game's message loop is suspended inside MessageBox, but its window still
// receives activation and close messages. Keep that owner usable during errors.
namespace ErrorDialog
{
struct State
{
    HWND owner = nullptr, dialog = nullptr;
    bool closing = false;
};
inline thread_local State *current = nullptr;

inline bool Active() { return current != nullptr; }
inline bool RequestClose()
{
    if (!current) return false;
    current->closing = true;
    if (current->dialog) EndDialog(current->dialog, IDCANCEL);
    return true;
}

inline bool HandleOwnerMessage(UINT message, WPARAM wparam)
{
    if (!current) return false;
    if (message == WM_CLOSE ||
        (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_CLOSE) ||
        (message == WM_SYSKEYDOWN && wparam == VK_F4))
    {
        RequestClose();
        return true;
    }
    // No game/UI callbacks, minimisation or cursor hiding inside the modal loop.
    if (message == WM_SETCURSOR) { SetCursor(LoadCursor(nullptr, IDC_ARROW)); return true; }
    return message == WM_ACTIVATE || message == WM_ACTIVATEAPP ||
           (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
}

inline LRESULT CALLBACK OwnerWindow(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                   UINT_PTR id, DWORD_PTR)
{
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, OwnerWindow, id);
    if (HandleOwnerMessage(message, wp)) return 1;
    return DefSubclassProc(window, message, wp, lp);
}

inline LRESULT CALLBACK Activate(int code, WPARAM wparam, LPARAM lparam)
{
    if (current && (code == HCBT_CREATEWND || code == HCBT_ACTIVATE))
    {
        HWND dialog = reinterpret_cast<HWND>(wparam);
        char name[32] = {};
        GetClassNameA(dialog, name, sizeof(name));
        if (lstrcmpA(name, "#32770") == 0)
        {
            current->dialog = dialog;
            // Ownership supplies z-order without MessageBox disabling the game:
            // its close button and Alt+F4 must remain available during errors.
            if (code == HCBT_ACTIVATE && IsWindow(current->owner))
                SetWindowLongPtr(dialog, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(current->owner));
            ClipCursor(nullptr);
            ReleaseCapture();
            if (code == HCBT_ACTIVATE)
            {
                SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                // HCBT_ACTIVATE runs before activation; USER32 completes focus
                // after the hook returns. MB_SETFOREGROUND requests activation.
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

class Scope
{
    State state;
    State *previous;
    HHOOK hook;
    RECT clip = {};
    bool haveClip;
    HWND capture;
    HCURSOR cursor;
    int showCalls = 0;
    bool resume;
public:
    Scope(HWND owner, bool continuing) : previous(current),
        haveClip(GetClipCursor(&clip) != FALSE), capture(GetCapture()), cursor(GetCursor()), resume(continuing)
    {
        state.owner = owner;
        current = &state;
        // Install last, ahead of renderer/UI input subclasses, for this scope.
        if (IsWindow(owner))
            SetWindowSubclass(owner, OwnerWindow, reinterpret_cast<UINT_PTR>(&state), 0);
        ClipCursor(nullptr);
        ReleaseCapture();
        do { ++showCalls; } while (ShowCursor(TRUE) < 0);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        hook = SetWindowsHookEx(WH_CBT, Activate, nullptr, GetCurrentThreadId());
    }
    ~Scope()
    {
        if (hook) UnhookWindowsHookEx(hook);
        if (IsWindow(state.owner))
            RemoveWindowSubclass(state.owner, OwnerWindow, reinterpret_cast<UINT_PTR>(&state));
        HWND foreground = GetForegroundWindow();
        // Never steal focus or restrict the pointer over another application.
        if (resume && !state.closing && IsWindow(state.owner) && !IsIconic(state.owner) &&
            (foreground == state.owner || foreground == state.dialog))
        {
            SetForegroundWindow(state.owner);
            SetFocus(state.owner);
            if (haveClip) ClipCursor(&clip);
            if (IsWindow(capture)) SetCapture(capture);
            SetCursor(cursor);
        }
        else { ClipCursor(nullptr); ReleaseCapture(); }
        while (showCalls-- > 0) ShowCursor(FALSE);
        current = previous;
        if (state.closing)
        {
            if (previous) RequestClose();
            else PostQuitMessage(0);
        }
    }
};

inline int ShowA(HWND owner, const char *text, const char *title, UINT type, bool resume = true)
{
    Scope scope(owner, resume);
    return MessageBoxA(nullptr, text, title, (type & ~MB_TASKMODAL) | MB_TOPMOST | MB_SETFOREGROUND);
}
inline int ShowW(HWND owner, const wchar_t *text, const wchar_t *title, UINT type, bool resume = true)
{
    Scope scope(owner, resume);
    return MessageBoxW(nullptr, text, title, (type & ~MB_TASKMODAL) | MB_TOPMOST | MB_SETFOREGROUND);
}
}
