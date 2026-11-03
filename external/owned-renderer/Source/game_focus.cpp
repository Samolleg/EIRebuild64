#include "game_focus.h"
#include "debug_overlay.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace owned {

void bridge_report_event(const char* event, const char* detail) noexcept;
void bridge_note_owned_patch(const void* address, const void* original, std::size_t bytes) noexcept;

namespace {

// game.exe sites (also checked in the file by game_sites_table.h, group
// focus; bytes identical in the Starter, GOG and Dejavu copies). WinMain's
// loop, with the world-clock step inlined into it:
//   while no message: update the render-enable state; step the world clock
//     (clock read 004571DC); if the window counts as active, idle the interface and
//     draw a frame (004575FD);
//   then GetMessage and hand the message to the full message handler
//   (00457641) when active, else to the window class procedure (0045765E).
constexpr std::uint32_t kCountTimeClockCallRva = 0x000571dc;  // world clock step: call [timeGetTime], FF 15 imm32
constexpr std::uint32_t kTimeGetTimeIatRva = 0x0033b6a0;      // game.exe timeGetTime import slot
constexpr std::uint32_t kMessageHandlerCallRva = 0x00057641;  // call message handler (fastcall msg, wparam; lparam pushed, callee pops)
constexpr std::uint32_t kWindowProcEntryRva = 0x00057d40;     // window class procedure (stdcall)
constexpr std::uint32_t kDrawFrameCallRva = 0x000575fd;       // call draw frame (fastcall flip in cl, draw-2D in dl)
// The render-enable byte: whether the game draws and steps its world. Its only
// writers are the window procedure's WM_ACTIVATE paths (1 on WA_ACTIVE and
// WA_CLICKACTIVE, 0 on WA_INACTIVE and on any activation of a minimised
// window); the WA_INACTIVE store is checked to prove the address.
constexpr std::uint32_t kInactiveRenderStoreRva = 0x00057e41;  // mov byte [render enable], 0
constexpr std::uint32_t kEnableRenderRva = 0x0039bc5c;
// The WA_ACTIVE path resets the interface's held-key, held-button and
// camera-motion state: mov ecx, offset interface; call reset (thiscall).
constexpr std::uint32_t kInterfaceResetCallRva = 0x00057e26;
constexpr std::uint32_t kInterfaceRva = 0x003c7d50;            // the interface object (CInterface3D)
constexpr std::uint32_t kInputResetRva = 0x001f8c50;           // its input-state reset (thiscall)

// Shipped loop boundary: last-time was updated before this no-argument call.
constexpr std::uint32_t kEditorPauseCallRva = 0x00057252;
constexpr std::uint32_t kEditorWallClockRva = 0x000122d0;
constexpr std::uint32_t kEditorIdleResumeRva = 0x000575e9;
constexpr std::uint32_t kMissionPointerRva = 0x00239d60;

using ClockFn = DWORD(WINAPI*)();
using MessageHandlerFn = LRESULT(__fastcall*)(UINT, WPARAM, LPARAM);
using WindowProcFn = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using DrawFrameFn = void(__fastcall*)(bool, bool);
using InputResetFn = void(__thiscall*)(void*);

HWND volatile g_window = nullptr;
std::atomic<int> g_mode{static_cast<int>(BackgroundMode::keep_running)};
bool g_harness = false;
bool g_installed = false;
// Keep the exact preceding owner of each call (including the bridge timing
// wrappers). A failed later write must restore the earlier calls too.
struct FocusCallPatch {
    std::uint8_t* address = nullptr;
    std::size_t length = 0;
    DWORD protection = 0;
    std::uint8_t original[6]{}, current[6]{};
};
FocusCallPatch g_patches[5];
std::size_t g_patch_count = 0;
std::uint8_t* g_image = nullptr;
ClockFn* g_clock_slot = nullptr;   // the import slot, read at every call
ClockFn g_next_clock = nullptr;    // a call-site hook that was there before ours
MessageHandlerFn g_next_message_handler = nullptr;
WindowProcFn g_window_proc = nullptr;
DrawFrameFn g_next_draw_frame = nullptr;
const volatile std::uint8_t* g_enable_render = nullptr;
InputResetFn g_input_reset = nullptr;
void* g_interface = nullptr;

// One authoritative editor state, changed on the game's interface thread.
std::uint8_t g_hud_editing=0;
void* g_mission_pointer_forward=nullptr;
void* g_editor_idle_resume=nullptr;
void(*g_editor_wall_clock)()=nullptr;
bool g_editor_ready=false;

__declspec(naked) void editor_count_time_gate() {
    __asm {
        call dword ptr [g_editor_wall_clock]
        cmp byte ptr [g_hud_editing],0
        je normal_return
        add esp,4
        jmp dword ptr [g_editor_idle_resume]
    normal_return:
        ret
    }
}
__declspec(naked) void editor_mission_pointer_gate() {
    __asm {
        pushfd
        cmp byte ptr [g_hud_editing],0
        jne blocked
        popfd
        jmp dword ptr [g_mission_pointer_forward]
    blocked:
        popfd
        ret 12
    }
}

// World clock offset: the time spent paused, which CountTime never sees.
// Only WinMain's thread reads the clock through this site.
DWORD g_clock_offset = 0;
DWORD g_pause_began = 0;
bool g_clock_paused = false;

bool world_paused() noexcept {
    return !g_harness && g_mode.load(std::memory_order_relaxed) == static_cast<int>(BackgroundMode::pause) &&
           !game_window_in_front();
}

DWORD WINAPI focus_count_time_clock() {
    const DWORD now = g_next_clock ? g_next_clock() : (*g_clock_slot)();
    if (world_paused()) {
        if (!g_clock_paused) {
            g_clock_paused = true;
            g_pause_began = now;
        }
        return g_pause_began - g_clock_offset;
    }
    if (g_clock_paused) {
        g_clock_offset += now - g_pause_began;
        g_clock_paused = false;
    }
    return now - g_clock_offset;
}

bool input_message(UINT message) noexcept {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == 0xcc54u;
}

LRESULT __fastcall focus_message_handler(UINT message, WPARAM wparam, LPARAM lparam) {
    const HWND window = g_window;
    // The native full dispatcher acts on clicks before reaching the HWND
    // subclass. Deliver renderer-control input to that subclass first.
    if(input_message(message) && (g_hud_editing || renderer_controls_capture_input(window))) {
        if(!window)return 0;
        const LRESULT result=SendMessageW(window,message,wparam,lparam);
        // The game's loop does not translate keys in standard mode (GetMessage
        // straight into this handler, no TranslateMessage), so renderer text
        // fields (the F10 filter) never received characters. Each key-down's
        // characters are made here, with the thread's keyboard state, and
        // delivered as WM_CHAR; ToUnicode flag 4 leaves the dead-key state
        // alone. Where the loop does translate (core mode typed every
        // character twice), TranslateMessage has already queued the key's
        // WM_CHAR behind it: that one is delivered and none is made.
        MSG queued{};
        if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&
           !PeekMessageW(&queued,window,WM_CHAR,WM_SYSDEADCHAR,PM_NOREMOVE)){
            BYTE state[256]{};
            if(GetKeyboardState(state)){
                WCHAR characters[4]{};
                const int count=ToUnicode(static_cast<UINT>(wparam),static_cast<UINT>((lparam>>16)&0xff),state,characters,4,4u);
                for(int index=0;index<count;++index)SendMessageW(window,WM_CHAR,characters[index],lparam);
            }
        }
        return result;
    }
    // The message handler answers Alt+Tab's key-up by calling the window
    // procedure's WA_INACTIVE path itself; the real WM_ACTIVATE decides
    // instead. While another window is in front, keyboard and mouse (a
    // pointer crossing the visible window, a wheel over it) do not reach the
    // interface, as in the original's inactive loop, which hands every
    // message to the window procedure.
    const bool alt_tab_up = message == WM_SYSKEYUP && wparam == VK_TAB && (lparam & 0x20000000) != 0;
    if (window && !g_harness && (alt_tab_up || (input_message(message) && !game_window_in_front())))
        return g_window_proc(window, message, wparam, lparam);
    return g_next_message_handler(message, wparam, lparam);
}

void __fastcall focus_draw_frame(bool flip, bool draw_2d) {
    // Paused: nothing is drawn, so the swap chain keeps the last presented
    // frame on screen and nothing is created, restored or cleared.
    if (world_paused()) {
        Sleep(50);
        return;
    }
    g_next_draw_frame(flip, draw_2d);
}

std::uint8_t* call_target(std::uint8_t* call) noexcept {
    if (call[0] != 0xe8) return nullptr;
    std::int32_t relative = 0;
    std::memcpy(&relative, call + 1, sizeof(relative));
    return call + 5 + relative;
}

bool absolute_operand(const std::uint8_t* at, std::uint32_t rva) noexcept {
    const auto expected = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_image + rva));
    std::uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value == expected;
}

bool write_call(std::uint8_t* at, const void* target, std::size_t length,std::uint8_t opcode=0xe8) noexcept {
    if (g_patch_count == 5 || length < 5 || length > 6) return false;
    auto& patch = g_patches[g_patch_count];
    patch.address = at;
    patch.length = length;
    std::memcpy(patch.original, at, length);
    std::memset(patch.current, 0x90, length);
    patch.current[0] = opcode;
    const auto relative = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(at + 5));
    std::memcpy(patch.current + 1, &relative, sizeof(relative));
    if (!VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &patch.protection)) return false;
    std::memcpy(at, patch.current, length);
    ++g_patch_count;  // journal even when protection restoration/flush fails
    DWORD ignored = 0;
    const bool restored = VirtualProtect(at, length, patch.protection, &ignored) != FALSE;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), at, length) != FALSE;
    bridge_note_owned_patch(at, patch.original, length);  // ownership verification
    return restored && flushed;
}

bool restore_calls() noexcept {
    while (g_patch_count) {
        auto& patch = g_patches[g_patch_count - 1];
        if (std::memcmp(patch.address, patch.current, patch.length) != 0) return false;
        DWORD previous = 0, ignored = 0;
        if (!VirtualProtect(patch.address, patch.length, PAGE_EXECUTE_READWRITE, &previous)) return false;
        std::memcpy(patch.address, patch.original, patch.length);
        std::memcpy(patch.current, patch.original, patch.length);
        const bool restored = VirtualProtect(patch.address, patch.length, patch.protection, &ignored) != FALSE;
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), patch.address, patch.length) != FALSE;
        if (!restored || !flushed) return false;
        --g_patch_count;
    }
    return true;
}

} // namespace

void set_game_focus_window(HWND window) noexcept { g_window = window; }
bool game_hud_editor_available() noexcept {return g_installed && g_editor_ready;}
bool game_hud_editing() noexcept {return g_hud_editing!=0;}
void set_game_hud_editing(bool enabled) noexcept {
    enabled=enabled && game_hud_editor_available();
    if(enabled==bool(g_hud_editing))return;
    if(g_input_reset && g_interface)g_input_reset(g_interface);
    g_hud_editing=enabled?1:0;
}


void set_background_mode(int mode) noexcept {
    g_mode.store(mode == static_cast<int>(BackgroundMode::pause) ? mode : 0, std::memory_order_relaxed);
}

bool game_window_in_front() noexcept {
    const HWND window = g_window;
    return !window || GetForegroundWindow() == window;
}

void install_game_focus(bool sites_ok) noexcept {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    if (!sites_ok) {
        bridge_report_event("game_focus_rejected", "site bytes differ in game.exe; the game keeps its own lost-focus path");
        return;
    }
    g_image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    g_harness = GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR", nullptr, 0) > 0;
    std::uint8_t* const clock = g_image + kCountTimeClockCallRva;
    std::uint8_t* const handler = g_image + kMessageHandlerCallRva;
    std::uint8_t* const frame = g_image + kDrawFrameCallRva;
    const std::uint8_t* const store = g_image + kInactiveRenderStoreRva;
    std::uint8_t* const reset = g_image + kInterfaceResetCallRva;

    // Live bytes: the stock instruction, or a call another hook put there,
    // which is then chained (the outer frame-timing profile hooks the same
    // three sites and installs first).
    const bool clock_stock = clock[0] == 0xff && clock[1] == 0x15 && absolute_operand(clock + 2, kTimeGetTimeIatRva);
    const bool clock_chained = clock[0] == 0xe8 && clock[5] == 0x90;
    const char* failure = nullptr;
    if (!clock_stock && !clock_chained) failure = "clock_call";
    else if (!call_target(handler)) failure = "message_handler_call";
    else if (!call_target(frame)) failure = "draw_frame_call";
    else if (store[0] != 0xc6 || store[1] != 0x05 || !absolute_operand(store + 2, kEnableRenderRva) || store[6] != 0)
        failure = "enable_render_store";
    else if (reset[0] != 0xb9 || !absolute_operand(reset + 1, kInterfaceRva) ||
             call_target(reset + 5) != g_image + kInputResetRva)
        failure = "interface_reset_call";
    if (failure) {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "live bytes differ at %s; the game keeps its own lost-focus path", failure);
        bridge_report_event("game_focus_rejected", detail);
        return;
    }
    g_clock_slot = reinterpret_cast<ClockFn*>(g_image + kTimeGetTimeIatRva);
    g_next_clock = clock_chained ? reinterpret_cast<ClockFn>(call_target(clock)) : nullptr;
    g_next_message_handler = reinterpret_cast<MessageHandlerFn>(call_target(handler));
    g_window_proc = reinterpret_cast<WindowProcFn>(g_image + kWindowProcEntryRva);
    g_next_draw_frame = reinterpret_cast<DrawFrameFn>(call_target(frame));
    g_enable_render = g_image + kEnableRenderRva;
    g_interface = g_image + kInterfaceRva;
    g_input_reset = reinterpret_cast<InputResetFn>(g_image + kInputResetRva);
    // Before the game's message loop starts (DirectDrawCreate, main thread).
    const bool wrote = write_call(clock, reinterpret_cast<const void*>(&focus_count_time_clock), 6) &&
                       write_call(handler, reinterpret_cast<const void*>(&focus_message_handler), 5) &&
                       write_call(frame, reinterpret_cast<const void*>(&focus_draw_frame), 5);
    if (!wrote) {
        const bool restored = uninstall_game_focus();
        bridge_report_event("game_focus_rejected", restored ? "call write failed; preceding calls restored" :
                            "call write failed; restoration blocked, hook records retained");
        return;
    }
    auto* pause=g_image+kEditorPauseCallRva;
    auto* pointer=g_image+kMissionPointerRva;
    const std::uint8_t pointer_bytes[]={0x53,0x56,0x8b,0xf1,0x57};
    const std::uint8_t idle_bytes[]={0xb9,0x50,0x7d,0x7c,0x00,0xb3,0x01,0xe8,0xcb,0xe1,0x19,0x00};
    const bool editor_stock=call_target(pause)==g_image+kEditorWallClockRva &&
        !std::memcmp(pointer,pointer_bytes,sizeof(pointer_bytes)) &&
        !std::memcmp(g_image+kEditorIdleResumeRva,idle_bytes,sizeof(idle_bytes));
    if(editor_stock) {
        auto* forward=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(forward) {
            std::memcpy(forward,pointer,5);forward[5]=0xe9;
            const auto relative=static_cast<std::int32_t>(pointer+5-(forward+10));
            std::memcpy(forward+6,&relative,4);DWORD old=0;
            if(VirtualProtect(forward,10,PAGE_EXECUTE_READ,&old) && FlushInstructionCache(GetCurrentProcess(),forward,10)) {
                g_mission_pointer_forward=forward;
                g_editor_wall_clock=reinterpret_cast<void(*)()>(call_target(pause));
                g_editor_idle_resume=g_image+kEditorIdleResumeRva;
                g_editor_ready=write_call(pause,reinterpret_cast<const void*>(&editor_count_time_gate),5) &&
                    write_call(pointer,reinterpret_cast<const void*>(&editor_mission_pointer_gate),5,0xe9);
            } else VirtualFree(forward,0,MEM_RELEASE);
        }
    }
    if(!g_editor_ready) {
        if(g_patch_count>3) {
            const bool restored=uninstall_game_focus();
            bridge_report_event("hud_editor_rejected",restored?"hook write failed; preceding owners restored":"hook restoration blocked; owners retained");
            return;
        }
        if(g_mission_pointer_forward){VirtualFree(g_mission_pointer_forward,0,MEM_RELEASE);g_mission_pointer_forward=nullptr;}
        bridge_report_event("hud_editor_rejected","pause/input sites unavailable; layout unlocking disabled");
    }
    g_installed = true;
    char detail[160];
    std::snprintf(detail, sizeof(detail), "clock=%s harness=%d", clock_chained ? "chained" : "stock",
                  g_harness ? 1 : 0);
    bridge_report_event("game_focus_installed", detail);
}

bool uninstall_game_focus() noexcept {
    set_game_hud_editing(false);
    if (!restore_calls()) {
        bridge_report_event("game_focus_uninstall_blocked", "call ownership or memory protection changed; records retained");
        return false;
    }
    g_installed = false;g_editor_ready=false;
    if(g_mission_pointer_forward)VirtualFree(g_mission_pointer_forward,0,MEM_RELEASE);
    g_mission_pointer_forward=nullptr;g_editor_idle_resume=nullptr;g_editor_wall_clock=nullptr;
    g_next_clock = nullptr;
    g_next_message_handler = nullptr;
    g_next_draw_frame = nullptr;
    g_clock_slot = nullptr;
    g_window_proc = nullptr;
    g_enable_render = nullptr;
    g_input_reset = nullptr;
    g_interface = nullptr;
    return true;
}

bool game_focus_swallow_activation(HWND window, UINT message, WPARAM wparam) noexcept {
    if (message != WM_ACTIVATE || !g_installed || !window || window != g_window) return false;
    // Not running yet (first activation) or already in its inactive state:
    // the game's own path, unchanged.
    if (!*g_enable_render) return false;
    // Running: it stays running whichever way the activation goes, so none
    // of the window procedure's minimise, sound pause, DirectInput unacquire,
    // surface restore, texture and cursor reload happens. Its input reset
    // does, on the way out (no key-up reaches a window in the background)
    // and back.
    g_input_reset(g_interface);
    char detail[96];
    std::snprintf(detail, sizeof(detail), "state=%u minimised=%u mode=%d", LOWORD(wparam), HIWORD(wparam),
                  g_mode.load(std::memory_order_relaxed));
    bridge_report_event("game_activation_swallowed", detail);
    return true;
}

} // namespace owned
