#include "game_focus.h"
#include "debug_overlay.h"
#include "game_sites.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace owned {

void bridge_report_event(const char* event, const char* detail) noexcept;
void bridge_note_owned_patch(const void* address, const void* original, std::size_t bytes) noexcept;
bool bridge_owns_count_time_clock(const void* target) noexcept;
bool bridge_lia_hud_editor_contract_matches() noexcept;
bool bridge_reset_lia_hud_editor_input(void* interface_object) noexcept;

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

struct FocusSites {
    std::uint32_t clock, clock_iat, handler, window_proc, frame, inactive_store;
    std::uint32_t enable_render, reset_call, interface_object, reset;
    std::uint32_t pause, wall_clock, idle_resume, mission_pointer;
    std::size_t pointer_bytes;
    bool clock_pointer_load;
};
const FocusSites& focus_sites() noexcept {
    static constexpr FocusSites base{kCountTimeClockCallRva,kTimeGetTimeIatRva,
        kMessageHandlerCallRva,kWindowProcEntryRva,kDrawFrameCallRva,kInactiveRenderStoreRva,
        kEnableRenderRva,kInterfaceResetCallRva,kInterfaceRva,kInputResetRva,
        kEditorPauseCallRva,kEditorWallClockRva,kEditorIdleResumeRva,kMissionPointerRva,5,false};
    static constexpr FocusSites lia{0x0003314b,0x00220544,0x00034eb2,0x00033570,
        0x00034e7c,0x00033679,0x00276810,0x00033663,0x002b8a38,0x0012fac0,
        0x000331d8,0x00013930,0x00033567,0x00176e50,9,true};
    return running_game_sites().report.engine == GameEngine::lia ? lia : base;
}

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
    std::uint8_t original[9]{}, current[9]{};
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
// LiA: its reset 0052FAC0 first clears five interface input bytes (+7C, +7E,
// +7D, +80, +7F), then tail-calls the top screen's +94 input owner, the part
// that faulted outside native activation (4c5a33a). Only the byte stores are
// repeated here, after the entry bytes match.
bool g_lia_input_clear = false;
constexpr std::uint8_t kLiaInputResetEntry[] = {
    0x32,0xc0,0x88,0x41,0x7c,0x88,0x41,0x7e,0x88,0x41,0x7d,0x88,0x81,0x80,0x00,0x00,0x00,0x88,0x41,0x7f};
void clear_lia_interface_input() noexcept {
    auto* interface_bytes = static_cast<volatile std::uint8_t*>(g_interface);
    for (const std::size_t offset : {0x7cu, 0x7eu, 0x7du, 0x80u, 0x7fu}) interface_bytes[offset] = 0;
}

// The native gates read this same one-byte value. Overlay callers may run on
// the scene thread; LiA input-owner work is consumed by the game clock hook.
std::atomic<std::uint8_t> g_hud_editing{0};
static_assert(sizeof(g_hud_editing)==1 && std::atomic<std::uint8_t>::is_always_lock_free);
std::atomic<int> g_lia_editor_request{-1};
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
    if(g_lia_editor_request.load(std::memory_order_acquire)>=0) {
        const int request=g_lia_editor_request.exchange(-1,std::memory_order_acquire);
        if(request>=0 && bool(request)!=bool(g_hud_editing.load(std::memory_order_relaxed))) {
            const bool reset=bridge_reset_lia_hud_editor_input(g_interface);
            if(reset || !request)g_hud_editing.store(request?1:0,std::memory_order_release);
            else bridge_report_event("hud_editor_rejected","LiA gameplay HUD is not the completed active top screen");
        }
    }
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
    if (g_patch_count == 5 || length < 5 || length > sizeof(g_patches[0].original)) return false;
    auto& patch = g_patches[g_patch_count];
    patch.address = at;
    patch.length = length;
    std::memcpy(patch.original, at, length);
    std::memset(patch.current, 0x90, length);
    patch.current[0] = opcode;
    // LiA loads the clock function into ESI; preserve both following CALL ESI sites.
    const std::uint32_t operand = opcode == 0xbe ?
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) :
        static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(target) -
                                   reinterpret_cast<std::intptr_t>(at + 5));
    std::memcpy(patch.current + 1, &operand, sizeof(operand));
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
    if(running_game_sites().report.engine==GameEngine::lia) {
        g_lia_editor_request.store(enabled?1:0,std::memory_order_release);
        return;
    }
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
    const auto& sites = focus_sites();
    g_image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    g_harness = GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR", nullptr, 0) > 0;
    std::uint8_t* const clock = g_image + sites.clock;
    std::uint8_t* const handler = g_image + sites.handler;
    std::uint8_t* const frame = g_image + sites.frame;
    const std::uint8_t* const store = g_image + sites.inactive_store;
    std::uint8_t* const reset = g_image + sites.reset_call;

    // Live bytes: the stock instruction, or a call another hook put there,
    // which is then chained (the outer frame-timing profile hooks the same
    // three sites and installs first).
    const bool clock_stock = clock[0] == (sites.clock_pointer_load ? 0x8b : 0xff) &&
        clock[1] == (sites.clock_pointer_load ? 0x35 : 0x15) && absolute_operand(clock + 2, sites.clock_iat);
    ClockFn preceding_clock = nullptr;
    if (sites.clock_pointer_load && clock[0] == 0xbe && clock[5] == 0x90)
        std::memcpy(&preceding_clock,clock+1,sizeof(preceding_clock));
    const bool clock_chained = sites.clock_pointer_load ?
        preceding_clock && bridge_owns_count_time_clock(reinterpret_cast<const void*>(preceding_clock)) :
        clock[0] == 0xe8 && clock[5] == 0x90;
    const char* failure = nullptr;
    if (!clock_stock && !clock_chained) failure = "clock_call";
    else if (!call_target(handler)) failure = "message_handler_call";
    else if (!call_target(frame)) failure = "draw_frame_call";
    else if (store[0] != 0xc6 || store[1] != 0x05 || !absolute_operand(store + 2, sites.enable_render) || store[6] != 0)
        failure = "enable_render_store";
    else if (reset[0] != 0xb9 || !absolute_operand(reset + 1, sites.interface_object) ||
             call_target(reset + 5) != g_image + sites.reset)
        failure = "interface_reset_call";
    if (failure) {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "live bytes differ at %s; the game keeps its own lost-focus path", failure);
        bridge_report_event("game_focus_rejected", detail);
        return;
    }
    g_clock_slot = reinterpret_cast<ClockFn*>(g_image + sites.clock_iat);
    g_next_clock = clock_chained ? (sites.clock_pointer_load ? preceding_clock :
        reinterpret_cast<ClockFn>(call_target(clock))) : nullptr;
    g_next_message_handler = reinterpret_cast<MessageHandlerFn>(call_target(handler));
    g_window_proc = reinterpret_cast<WindowProcFn>(g_image + sites.window_proc);
    g_next_draw_frame = reinterpret_cast<DrawFrameFn>(call_target(frame));
    g_enable_render = g_image + sites.enable_render;
    g_interface = g_image + sites.interface_object;
    // LiA's reset also calls the current screen's embedded input owner. The
    // shipped WA_ACTIVE caller performs native activation work first; arbitrary
    // activation calls bypass it (4c5a33a fault at reset+0x2f). HUD editing
    // separately admits only a completed top-level gameplay HUD.
    const bool input_reset_supported = running_game_sites().report.engine != GameEngine::lia;
    g_input_reset = input_reset_supported ? reinterpret_cast<InputResetFn>(g_image + sites.reset) : nullptr;
    g_lia_input_clear = !input_reset_supported &&
        std::memcmp(g_image + sites.reset, kLiaInputResetEntry, sizeof(kLiaInputResetEntry)) == 0;
    // Before the game's message loop starts (DirectDrawCreate, main thread).
    const bool wrote = write_call(clock, reinterpret_cast<const void*>(&focus_count_time_clock), 6,
                                  sites.clock_pointer_load ? 0xbe : 0xe8) &&
                       write_call(handler, reinterpret_cast<const void*>(&focus_message_handler), 5) &&
                       write_call(frame, reinterpret_cast<const void*>(&focus_draw_frame), 5);
    if (!wrote) {
        const bool restored = uninstall_game_focus();
        bridge_report_event("game_focus_rejected", restored ? "call write failed; preceding calls restored" :
                            "call write failed; restoration blocked, hook records retained");
        return;
    }
    auto* pause=g_image+sites.pause;
    auto* pointer=g_image+sites.mission_pointer;
    const std::uint8_t base_pointer[]={0x53,0x56,0x8b,0xf1,0x57};
    const std::uint8_t lia_pointer[]={0x56,0x8b,0xf1,0x8b,0x86,0x98,0x01,0x00,0x00};
    const std::uint8_t base_idle[]={0xb9,0x50,0x7d,0x7c,0x00,0xb3,0x01,0xe8,0xcb,0xe1,0x19,0x00};
    const std::uint8_t lia_idle[]={0xb0,0x01,0x5e,0x83,0xc4,0x08,0xc3};
    const std::uint8_t lia_frame[]={0xa0,0x5c,0x68,0x67,0x00,0x83,0xec,0x08,0xa8,0x01,0x56};
    const bool editor_reset_supported=input_reset_supported || bridge_lia_hud_editor_contract_matches();
    const bool editor_stock=editor_reset_supported && call_target(pause)==g_image+sites.wall_clock &&
        !std::memcmp(pointer,sites.clock_pointer_load?lia_pointer:base_pointer,sites.pointer_bytes) &&
        !std::memcmp(g_image+sites.idle_resume,sites.clock_pointer_load?lia_idle:base_idle,
                     sites.clock_pointer_load?sizeof(lia_idle):sizeof(base_idle)) &&
        (!sites.clock_pointer_load || !std::memcmp(g_image+0x00033140,lia_frame,sizeof(lia_frame)));
    if(editor_stock) {
        auto* forward=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,sites.pointer_bytes+5,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(forward) {
            std::memcpy(forward,pointer,sites.pointer_bytes);forward[sites.pointer_bytes]=0xe9;
            const auto relative=static_cast<std::int32_t>(pointer+sites.pointer_bytes-(forward+sites.pointer_bytes+5));
            std::memcpy(forward+sites.pointer_bytes+1,&relative,4);DWORD old=0;
            if(VirtualProtect(forward,sites.pointer_bytes+5,PAGE_EXECUTE_READ,&old) &&
               FlushInstructionCache(GetCurrentProcess(),forward,sites.pointer_bytes+5)) {
                g_mission_pointer_forward=forward;
                g_editor_wall_clock=reinterpret_cast<void(*)()>(call_target(pause));
                g_editor_idle_resume=g_image+sites.idle_resume;
                g_editor_ready=write_call(pause,reinterpret_cast<const void*>(&editor_count_time_gate),5) &&
                    write_call(pointer,reinterpret_cast<const void*>(&editor_mission_pointer_gate),sites.pointer_bytes,0xe9);
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
    if (!input_reset_supported)
        bridge_report_event(g_lia_input_clear ? "game_activation_override_installed" : "game_activation_override_rejected",
            g_lia_input_clear ? "LiA keep-running: deactivation and its activation swallowed, interface input bytes cleared"
                              : "LiA input-reset entry differs; native activation and background behavior retained");
    char detail[160];
    std::snprintf(detail, sizeof(detail), "clock=%s harness=%d", clock_chained ? "chained" : "stock",
                  g_harness ? 1 : 0);
    bridge_report_event("game_focus_installed", detail);
}

// LiA: the last WM_ACTIVATE swallowed was a deactivation (an activation is
// swallowed only after one; reset with the hooks).
static bool lia_swallowed_deactivation = false;

bool uninstall_game_focus() noexcept {
    if(running_game_sites().report.engine==GameEngine::lia) {
        // Unload may run on another thread. Retire renderer state without
        // calling a native input owner from that thread.
        g_lia_editor_request.store(-1,std::memory_order_relaxed);
        g_hud_editing.store(0,std::memory_order_release);
    } else set_game_hud_editing(false);
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
    g_lia_input_clear = false;
    lia_swallowed_deactivation = false;
    g_interface = nullptr;
    return true;
}

bool game_focus_swallow_activation(HWND window, UINT message, WPARAM wparam) noexcept {
    if (message != WM_ACTIVATE || !g_installed || !window || window != g_window ||
        (!g_input_reset && !g_lia_input_clear)) return false;
    // LiA: the keep-running background mode (harness runs always keep running)
    // swallows; the pause mode keeps LiA's native minimise/pause path.
    if (g_lia_input_clear) {
        const bool keep_running = g_harness ||
            g_mode.load(std::memory_order_relaxed) != static_cast<int>(BackgroundMode::pause);
        const bool deactivation = LOWORD(wparam) == WA_INACTIVE || HIWORD(wparam) != 0;
        // An activation is swallowed only after its deactivation was: nothing was torn down.
        if (!keep_running || !*g_enable_render || (!deactivation && !lia_swallowed_deactivation)) return false;
        lia_swallowed_deactivation = deactivation;
        clear_lia_interface_input();
        char detail[96];
        std::snprintf(detail, sizeof(detail), "state=%u minimised=%u lia_input_bytes_cleared", LOWORD(wparam), HIWORD(wparam));
        bridge_report_event("game_activation_swallowed", detail);
        return true;
    }
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
