#include "test_harness.h"
#include "engine_bridge.h"
#include "addon_hook_chain.h"

#include <windows.h>
#include "game_sites.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

namespace owned {
namespace {

// Read from the disassembly of game.exe BE0FC330...94E2; a running exe is
// used only when these sites verify (game_sites.h, harness group):
//  - CCamera is the static object at 0x79B2A8; the game passes it as `this`
//    to the functions below (e.g. 006453C3, 00667CB6).
//  - 00411470 SetCameraAs(shortcut): fDistance(+0xF0) = s[0], fPitch(+0x104)
//    = s[0x14], and zeroes the scroll/rotate/pitch/zoom movements.
//  - 0040D420 SetCarrier(id): +0x11C = id; 0xFFFFFFFF detaches the camera.
//  - 0040F5C0 SetAnchorPoint(pt): anchor (+0xE0) = pt, then the anchor height
//    from the terrain. The minimap jump (006453B8..0064540A) does exactly
//    flags(+0x08) &= ~8, SetCarrier(-1), SetAnchorPoint({x, y, anchor.z}).
//  - 004109E0 InitRotate(radians): qRotate(+0xF4, w x y z) = rotation about
//    +Z by the angle (0049ECC0 halves it), then rebuilds the placement.
//  - The static CWorldClient 0x7AF5B8: hour = fmod(ticks(+0x64) *
//    scale(+0x20) + modifier(+0x1C), 24) at 00566386, inside
//    AdjustLightingWithTime (00566360), which CWorldClient::Draw calls at
//    00569E58 - after the Draw entry where harness_world_frame runs.
//  - 00455F20 QuitProgram(code), fastcall: PostQuitMessage(code) when the
//    game allows quitting; the game's own menus call it.
constexpr std::uintptr_t kCameraRva = 0x0039b2a8;
constexpr std::uintptr_t kWorldClientRva = 0x003af5b8;
constexpr std::uintptr_t kSetCameraAsRva = 0x00011470;
constexpr std::uintptr_t kSetCarrierRva = 0x0000d420;
constexpr std::uintptr_t kSetAnchorPointRva = 0x0000f5c0;
constexpr std::uintptr_t kInitRotateRva = 0x000109e0;
constexpr std::uintptr_t kQuitProgramRva = 0x00055f20;
// Binary-confirmed Load dialog call at 00655ADE..00655AEF and quickload at
// 00657664..0065766C: ECX=007AFE20, stack=(slot char*, false). 005816D0
// copies the string, queues action 2, writes the flag at +78, and RET 8.
// 0057C700 consumes the action between frames (004573C5), then clears it
// at 0057CB08. Requesting a load at Draw entry therefore cannot tear down
// the world currently being drawn.
constexpr std::uintptr_t kRequestLoadRva = 0x001816d0;
constexpr std::uintptr_t kLoadTransitionRva = 0x0017c700;
// Stock bodies. The Dejavu addon (from its 2026-10-25 build) hooks both
// entries through its hook builder (RequestLoad: 5 patched bytes, 0057C700:
// 6), so they are resolved when load_save runs (load_entry_error).
constexpr std::uint8_t kRequestLoadStock[] = {
    0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1,0x50,0x8d,0x4e,0x04,
    0xc7,0x06,0x02,0x00,0x00,0x00,0xe8,0x13,0xf4,0x17,0x00,
    0x8a,0x44,0x24,0x0c,0x33,0xc9,0x84,0xc0,0x0f,0x95,0xc1,
    0x89,0x4e,0x78,0x5e,0xc2,0x08,0x00};
constexpr std::uint8_t kLoadTransitionStock[] = {
    0x64,0xa1,0x00,0x00,0x00,0x00,0x6a,0xff,0x68,0x74,0x39,0x72,0x00,0x50,0x64,0x89};
constexpr std::uintptr_t kConnectorRva = 0x003afe20;
constexpr std::uintptr_t kSavePathCallRva = 0x00255d7e;
constexpr std::uintptr_t kLoadPathOperandRva = 0x0017ac4e;
constexpr std::uintptr_t kWorkPathCellRva = 0x003af460;
// The console Enter handler loads text in ECX and calls 005DC620 at
// 005E458F, with no stack argument. The parser ends in RET at 005DD7FA.
// Its addon-owned interior patches remain in place; no hook is installed.
constexpr std::uintptr_t kConsoleProcessRva = 0x001dc620;
constexpr std::uintptr_t kStringConstructRva = 0x003009de;
constexpr std::uintptr_t kStringDestroyRva = 0x00300970;
constexpr std::uintptr_t kDialogFormNameRva = 0x00253a50;
constexpr std::uintptr_t kCurrentZoneRva = 0x00178a90;
constexpr std::uintptr_t kClientInterfaceRva = 0x003afea5;
// Mission pause, as the KA_PAUSE key and the clock button set it: their
// handler 00648EE0 calls IWorld(007AFEA4).MissionCombatPause(!IsMissionCombatPause())
// at 00648F07/00648EF7. 00578D30 MissionCombatPause(bool) loads ECX=007AF5B8
// and calls 00566330 CWorldClient::CombatPause, which sends the request
// through [[ECX+20C]+20] (the player's client) to the server; the server's
// visible-units update returns its flag to the client. 00578D50
// IsMissionCombatPause reads the client flag 007AF624, 00578D00 IsCombatPause
// the server flag 007AFFCC. Setting the wanted value directly avoids a toggle.
constexpr std::uintptr_t kInterfaceRva = 0x003afea4;
constexpr std::uintptr_t kMissionPauseRva = 0x00178d30;
constexpr std::uintptr_t kIsMissionPauseRva = 0x00178d50;
constexpr std::uintptr_t kIsServerPauseRva = 0x00178d00;
constexpr std::uintptr_t kClientCombatPauseRva = 0x00166330;
constexpr std::size_t kWorldPlayer = 0x20c, kPlayerSpecific = 0x20;
// God mode, as the GodMode(player, value) script command sets it: its case at
// 004B6EDA pushes the player number, calls 00594180 CWorldServer::GetPlayer
// with ECX=007AFF60 (the server), and stores the value byte at [player+108].
// The server's attack processing ignores attacks on units of such a player.
constexpr std::uintptr_t kServerRva = 0x003aff60;
constexpr std::uintptr_t kGetServerPlayerRva = 0x00194180;
constexpr std::uintptr_t kGodModeScriptRva = 0x000b6eda;
constexpr std::size_t kPlayerGod = 0x108;
// The server's interface, as SaveGame tests it at 0057B4D1
// (CMP [007C2CC4],4: the world/travel map). That screen draws no world.
// 00581660 ReqGoMainMenu (ECX=007AFE20, the connector) drops the network
// chat objects and queues action 1, consumed between frames like a load;
// the game's own menus and the end screen call it (the Esc menu's "main
// menu" confirmation calls it directly). The supported addon may own the
// entry through its hook builder (jmp dispatcher + NOP fill; the Dejavu
// addon does): the harness then calls the entry exactly like those game
// callers, through the addon's chain, never around it. Its tail
// (+54: MOV [ESI],1; POP ESI; RET) stays stock either way.
constexpr std::uintptr_t kServerInterfaceRva = 0x003c2cc4;
constexpr std::uintptr_t kSaveInterfaceTestRva = 0x0017b4d1;
constexpr std::uintptr_t kRequestMainMenuRva = 0x00181660;
constexpr std::uint8_t kRequestMainMenuStock[] = {
    0x56,0x8b,0xf1,0x8b,0x0d,0xdc,0xb5,0x79,0x00,0xc6,0x05,0xd8,0x7d,0x7c,0x00,0x00};
constexpr std::uint32_t kInterfaceMainMenu = 2, kInterfaceTravelMap = 4;
constexpr const char* kTravelMapDetail = "game is on the world map (no mission); no world frames there";
// What the server sends the client (CWorldServer::UpdatePlayer 0058B750): the
// client reports the camera's observation point (the anchor, or the camera
// itself in placements) and the server sends only what lies within a 2D range
// of it; what it stops sending is removed on the client.
//  - Dynamic map objects that are not units (torches and campfires, levers):
//    dist2 < R*R, R the read-only float 0073F91C (100.0) loaded by the FLDs
//    at 0058BB26/0058BB2C and 0058BCD8/0058BCDE, its only readers.
//  - Effects (map particle sources, point lights, script lightning): range
//    from virtual slot +4C; CEffect::GetRange (006768F0: FLD [007433AC]; RET)
//    returns the read-only float 007433AC (100.0); its only other reader is
//    CEffectPointLight::GetRange (00677F70).
constexpr std::uintptr_t kMapObjectRangeRva = 0x0033f91c;
constexpr std::uintptr_t kEffectRangeRva = 0x003433ac;
constexpr std::uintptr_t kEffectGetRangeRva = 0x002768f0;
constexpr std::uintptr_t kMapObjectRangeLoadRva = 0x0018bb26;
constexpr std::uintptr_t kMapObjectRangeLoad2Rva = 0x0018bcd8;
constexpr float kStockVisualRange = 100.f;
// The rain's strike (CWorldInterface::InvokeLightning 00578E30, called by the
// server at 0058D114) formats its script and loads it with CScript::LoadText
// (004D23A0, thiscall (CString&, BOOL), RET 8) on the server's scriptTemp
// 007C29CC at 00579098..005790A0: PUSH 1; PUSH ECX(&text); MOV ECX,007C29CC;
// CALL. The script runs from the next server segment.
constexpr std::uintptr_t kScriptLoadTextRva = 0x000d23a0;
constexpr std::uintptr_t kServerScriptTempRva = 0x003c29cc;
constexpr std::uintptr_t kWeatherLoadTextCallRva = 0x00179098;
constexpr std::size_t kCameraFlags = 0x08, kCameraAnchor = 0xe0, kCameraDistance = 0xf0,
                      kCameraRotation = 0xf4, kCameraPitch = 0x104;
constexpr std::uint32_t kCameraJumpClearedFlag = 0x8;
constexpr std::size_t kWorldTimeModifier = 0x1c, kWorldTimeScale = 0x20, kWorldTicks = 0x64;
constexpr unsigned kWorldCommandTimeoutPresents = 600;
constexpr float kPi = 3.14159265358979f;

struct ExpectedEntry { std::uintptr_t rva; std::uint8_t bytes[8]; };
constexpr ExpectedEntry kEntries[] = {
    {kSetCameraAsRva, {0x8b, 0x44, 0x24, 0x04, 0x8b, 0x10, 0x89, 0x91}},
    {kSetCarrierRva, {0x8b, 0x44, 0x24, 0x04, 0x83, 0xf8, 0xff, 0x89}},
    {kSetAnchorPointRva, {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x08, 0x8b}},
    {kInitRotateRva, {0x83, 0xec, 0x0c, 0x8b, 0x44, 0x24, 0x10, 0x56}},
    {kQuitProgramRva, {0xa0, 0x51, 0x47, 0x78, 0x00, 0x84, 0xc0, 0x74}},
};

// LiA (game.exe C7B085C1): console, quit, list_saves, load_save and godmode
// are ported (entries below); its camera, pause and menu entries are not
// (initialize_game_commands, harness_world_frame). QuitProgram at 00432460 has base
// 00455F20's body (can-quit byte test, PUSH ECX, CALL [PostQuitMessage IAT
// 00620480], MOV AL,1, RET). The console Enter handler (005227C3..C8: MOV
// EAX,[ESI+10]; MOV ECX,[EAX]; CALL) calls 00424540 with the text in ECX and
// no stack argument; it registers the "help" family into the command map,
// tokenizes ECX (0047C030), gates '@' commands on the cheat byte 00673C90
// like base, and ends in a plain RET (004250D1).
constexpr std::uintptr_t kLiaQuitProgramRva = 0x00032460;
constexpr std::uint8_t kLiaQuitProgramStock[] = {
    0xa0,0x09,0x83,0x66,0x00,0x84,0xc0,0x74,0x0a,0x51,0xff,0x15,0x80,0x04,0x62,0x00,0xb0,0x01,0xc3};
constexpr std::uintptr_t kLiaConsoleProcessRva = 0x00024540;
constexpr std::uint8_t kLiaConsoleProcessStock[] = {
    0x6a,0xff,0x68,0x80,0xa1,0x60,0x00,0x64,0xa1,0x00,0x00,0x00,0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00,0x00,
    0x83,0xec,0x1c,0x55,0x56,0x57,0x8b,0xf9,0x68,0x70,0x1e,0x62,0x00};
constexpr std::uintptr_t kLiaConsoleCallRva = 0x001227c3;
constexpr std::uint8_t kLiaConsoleCallStock[] = {0x8b,0x46,0x10,0x8b,0x08,0xe8,0x73,0x1d,0xf0,0xff};
// LiA load_save, the same flow as base on these verified entries:
//  - RequestLoad 004DFA80 (thiscall connector, slot char*, bool; RET 8)
//    stores action 2 at [ECX], copies the slot to +8 and the flag to +7C. The
//    Load dialog calls it at 005812A9..B1 (PUSH 0; PUSH slot; MOV ECX,006A3260)
//    and quickload at 00581E78..80, like base 00655ADE/00657664.
//  - The between-frames consumer 004E63E0 (ECX = 006A3260, called at 00433351
//    like base 004573C0) tests [ECX] and the server interface 006B613C with
//    base 0057C700's sequence; SaveGame tests [006B613C] == 4 (world map) at
//    004E3BB1 like base 0057B4D1.
//  - The save path cell 006A2928 is what the save dialog's accessor 004DD600
//    (MOV EAX,006A2928; RET, called at 00585348) returns and what the native
//    loader reads at 004E333B (MOV ECX,[006A2928], its save folder format).
//  - The party zone accessor 004DEC20 (thiscall, out string; RET 4) reads the
//    world client 006A29D8's player +1C, its string at +D8, and stores an
//    add-referenced copy in the out slot (LiA strings: data with a 10h header;
//    released as the game does at 00424588: lock xadd -1 on header+0C, then
//    the manager [header]'s Free, vtable +4, at zero).
constexpr std::uintptr_t kLiaRequestLoadRva = 0x000dfa80;
constexpr std::uint8_t kLiaRequestLoadStock[] = {
    0x56,0x57,0x8b,0x7c,0x24,0x0c,0x85,0xff,0x8b,0xf1,0xc7,0x06,0x02,0x00,0x00,0x00,0x8d,0x4e,0x08,0x75,
    0x04,0x33,0xc0,0xeb,0x11,0x8b,0xc7,0x53,0x8d,0x58,0x01,0x90,0x8a,0x10,0x40,0x84,0xd2,0x75,0xf9,0x2b,
    0xc3,0x5b,0x50,0x57,0xe8,0x7f,0x43,0xf2,0xff,0x8a,0x4c,0x24,0x10,0x33,0xc0,0x84,0xc9,0x0f,0x95,0xc0,
    0x5f,0x89,0x46,0x7c,0x5e,0xc2,0x08,0x00};
constexpr std::uintptr_t kLiaLoadTransitionRva = 0x000e63e0;
constexpr std::uint8_t kLiaLoadTransitionStock[] = {
    0x64,0xa1,0x00,0x00,0x00,0x00,0x6a,0xff,0x68,0x03,0x1d,0x61,0x00,0x50,0x64,0x89};
constexpr std::uintptr_t kLiaLoadDialogCallRva = 0x001812a9;
constexpr std::uint8_t kLiaLoadDialogCallStock[] = {
    0x6a,0x00,0x50,0xb9,0x60,0x32,0x6a,0x00,0xe8,0xca,0xe7,0xf5,0xff};
constexpr std::uintptr_t kLiaConnectorRva = 0x002a3260;
constexpr std::uintptr_t kLiaServerInterfaceRva = 0x002b613c;
constexpr std::uintptr_t kLiaSaveInterfaceTestRva = 0x000e3bb1;
constexpr std::uint8_t kLiaSaveInterfaceTestStock[] = {0x83,0x3d,0x3c,0x61,0x6b,0x00,0x04};
constexpr std::uintptr_t kLiaSavePathCellRva = 0x002a2928;
constexpr std::uintptr_t kLiaSavePathAccessorRva = 0x000dd600;
constexpr std::uint8_t kLiaSavePathAccessorStock[] = {0xb8,0x28,0x29,0x6a,0x00,0xc3};
constexpr std::uintptr_t kLiaSavePathCallRva = 0x00185348;
constexpr std::uint8_t kLiaSavePathCallStock[] = {0xe8,0xb3,0x82,0xf5,0xff,0x8b,0x00,0x50};
constexpr std::uintptr_t kLiaLoadPathOperandRva = 0x000e333b;
constexpr std::uint8_t kLiaLoadPathOperandStock[] = {
    0x8b,0x0d,0x28,0x29,0x6a,0x00,0x50,0x51,0x8d,0x54,0x24,0x18,0x68,0x5c,0xd5,0x62,0x00};
constexpr std::uintptr_t kLiaCurrentZoneRva = 0x000dec20;
constexpr std::uintptr_t kLiaClientInterfaceRva = 0x002a3249;
constexpr std::uint8_t kLiaCurrentZoneStock[] = {
    0x51,0xb9,0xd8,0x29,0x6a,0x00,0xc7,0x44,0x24,0x00,0x00,0x00,0x00,0x00,0xe8,0x4d,0xff,0xfe,0xff,0x8b,
    0x40,0x1c,0x8b,0x88,0xd8,0x00,0x00,0x00,0x83,0xe9,0x10,0xe8,0xac,0x21,0xf3,0xff,0x8b,0xc8,0x8b,0x44,
    0x24,0x08,0x83,0xc1,0x10,0x89,0x08,0x59,0xc2,0x04,0x00};
// LiA god mode: the GodMode script case at 004716B2 has base 004B6EDA's
// shape (player number from argument 0, PUSH; MOV ECX,006A33D8 the server;
// CALL GetPlayer 004E6C40; value from argument 1 stored at [player+108]).
// GetPlayer (thiscall, number; RET 4) walks the server's players for +24 ==
// number.
constexpr std::uintptr_t kLiaGodModeScriptRva = 0x000716b2;
constexpr std::uint8_t kLiaGodModeScriptStock[] = {
    0x8b,0x46,0x08,0xd9,0x00,0xe8,0x3c,0xa1,0x17,0x00,0x50,0xb9,0xd8,0x33,0x6a,0x00,0xe8,0x79,0x55,0x07,
    0x00,0x8b,0xf8,0x3b,0xfd,0x0f,0x84,0x27,0x20,0x00,0x00,0x8b,0x4e,0x08,0xd9,0x41,0x04,0xe8,0x1c,0xa1,
    0x17,0x00,0x88,0x87,0x08,0x01,0x00,0x00};
constexpr std::uintptr_t kLiaServerRva = 0x002a33d8;
constexpr std::uintptr_t kLiaGetServerPlayerRva = 0x000e6c40;
constexpr std::uint8_t kLiaGetServerPlayerStock[] = {
    0x6a,0xff,0x68,0x38,0x1d,0x61,0x00,0x64,0xa1,0x00,0x00,0x00,0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00,
    0x00,0x83,0xec,0x08};
bool lia_harness() noexcept { return running_game_sites().report.engine == GameEngine::lia; }
std::uintptr_t connector_rva() noexcept { return lia_harness() ? kLiaConnectorRva : 0x003afe20; }

using SetCameraAsFn = void(__thiscall*)(void*, const float*);
using SetCarrierFn = void(__thiscall*)(void*, std::uint32_t);
using SetAnchorPointFn = void(__thiscall*)(void*, const float*);
using InitRotateFn = void(__thiscall*)(void*, float);
using QuitProgramFn = bool(__fastcall*)(int);
using RequestLoadFn = void(__thiscall*)(void*, const char*, bool);
using ConsoleProcessFn = void(__fastcall*)(const char*);
using SetPauseFn = void(__thiscall*)(void*, bool);
using IsPauseFn = bool(__thiscall*)(void*);
using GetServerPlayerFn = std::uint8_t*(__thiscall*)(void*, int);
using RequestMainMenuFn = void(__thiscall*)(void*);

struct Entry {
    unsigned long id = 0;
    std::string line;
    HarnessCommand command;
    unsigned progress = 0;
    bool posted = false;
    bool retried = false;
};

struct State {
    bool enabled = false;
    bool game_verified = false;
    bool quit_verified = false;  // base: game_verified; LiA: its own QuitProgram
    std::uint8_t* image = nullptr;
    std::string directory;
    std::deque<Entry> queue;
    unsigned long next_id = 1;
    // Hand-off to the world frame (both run on the game thread).
    bool world_pending = false;
    bool world_done = false;
    bool world_ok = false;
    std::string world_detail;
    std::string saves_error, load_error, console_error, pause_error, god_error, interface_error, menu_error;
    const char* const* save_path_cell = nullptr;
    bool load_waiting = false;
    HarnessLoadWait load_wait;
    std::string load_slot, load_zone;
    bool loaded_zone_empty = false;
    std::uint64_t load_started_ms = 0, menu_started_ms = 0;
    // Clock pin, re-applied at every Draw entry while active.
    bool clock_pinned = false;
    float clock_hour = 0.0f;
    float path_start[5]{}; // camera_path (temporary): the pose it starts from (camera order)
    unsigned long command_file_error = 0; // the last cmd.txt delete failure named (0: none)
    // Result lines not yet in results.txt (another process held it without
    // write sharing): the driver waits on each line, so none may be dropped.
    std::deque<std::string> unwritten_results;
    unsigned long result_file_error = 0; // the last results.txt open failure named (0: none)
    bool clock_saved = false;
    float saved_scale = 0.0f, saved_modifier = 0.0f;
};

std::string entry_error(const State& s, std::uintptr_t rva,
                        std::initializer_list<std::uint8_t> bytes) {
    if (std::memcmp(s.image + rva, bytes.begin(), bytes.size()) == 0) return {};
    char message[96]{};
    std::snprintf(message, sizeof(message), "game bytes differ at 0x%08lX",
                  static_cast<unsigned long>(0x00400000 + rva));
    return message;
}

void initialize_game_commands(State& s) {
    if (lia_harness()) {
        const auto same = [&s](std::uintptr_t rva, const std::uint8_t* bytes, std::size_t size) {
            return std::memcmp(s.image + rva, bytes, size) == 0; };
        s.pause_error = s.menu_error = "not ported on LiA";
        s.god_error = same(kLiaGodModeScriptRva, kLiaGodModeScriptStock, sizeof(kLiaGodModeScriptStock)) &&
                      same(kLiaGetServerPlayerRva, kLiaGetServerPlayerStock, sizeof(kLiaGetServerPlayerStock))
            ? "" : "LiA god mode sites differ";
        s.interface_error = same(kLiaSaveInterfaceTestRva, kLiaSaveInterfaceTestStock,
                                 sizeof(kLiaSaveInterfaceTestStock)) ? "" : "LiA SaveGame interface test differs";
        s.saves_error = same(kLiaSavePathAccessorRva, kLiaSavePathAccessorStock, sizeof(kLiaSavePathAccessorStock)) &&
                        same(kLiaSavePathCallRva, kLiaSavePathCallStock, sizeof(kLiaSavePathCallStock)) &&
                        same(kLiaLoadPathOperandRva, kLiaLoadPathOperandStock, sizeof(kLiaLoadPathOperandStock))
            ? "" : "LiA save path sites differ";
        if (s.saves_error.empty())
            s.save_path_cell = reinterpret_cast<const char* const*>(s.image + kLiaSavePathCellRva);
        s.load_error = s.saves_error;
        if (s.load_error.empty() && !(
                same(kLiaRequestLoadRva, kLiaRequestLoadStock, sizeof(kLiaRequestLoadStock)) &&
                same(kLiaLoadTransitionRva, kLiaLoadTransitionStock, sizeof(kLiaLoadTransitionStock)) &&
                same(kLiaLoadDialogCallRva, kLiaLoadDialogCallStock, sizeof(kLiaLoadDialogCallStock)) &&
                same(kLiaCurrentZoneRva, kLiaCurrentZoneStock, sizeof(kLiaCurrentZoneStock))))
            s.load_error = "LiA load entries differ";
        s.console_error = std::memcmp(s.image + kLiaConsoleProcessRva, kLiaConsoleProcessStock,
                                      sizeof(kLiaConsoleProcessStock)) != 0 ||
                          std::memcmp(s.image + kLiaConsoleCallRva, kLiaConsoleCallStock,
                                      sizeof(kLiaConsoleCallStock)) != 0
            ? "LiA console entry bytes differ" : "";
        return;
    }
    if (!game_sites_ok(GameSiteGroup::harness)) {
        s.saves_error = s.load_error = s.console_error = s.pause_error = s.god_error =
            s.interface_error = s.menu_error = "harness site bytes differ in game.exe";
        return;
    }
    s.console_error = entry_error(s, kConsoleProcessRva,
        {0x6a,0xff,0x68,0x80,0x89,0x72,0x00,0x64});
    s.pause_error = entry_error(s, kMissionPauseRva,
        {0x8b,0x44,0x24,0x04,0xb9,0xb8,0xf5,0x7a,0x00,0x50,0xe8,0xf1,0xd5,0xfe,0xff,0xc2,0x04,0x00});
    if (s.pause_error.empty()) s.pause_error = entry_error(s, kClientCombatPauseRva,
        {0x8b,0x89,0x0c,0x02,0x00,0x00,0x8b,0x44,0x24,0x04,0x50,0x8b,0x49,0x20,0xe8});
    if (s.pause_error.empty()) s.pause_error = entry_error(s, kIsMissionPauseRva,
        {0xa0,0x24,0xf6,0x7a,0x00,0xc3});
    if (s.pause_error.empty()) s.pause_error = entry_error(s, kIsServerPauseRva,
        {0xa0,0xcc,0xff,0x7a,0x00,0xc3});
    s.god_error = entry_error(s, kGodModeScriptRva,
        {0x8b,0x56,0x08,0xd9,0x02,0xe8,0x58,0x16,0x23,0x00,0x50,0xb9,0x60,0xff,0x7a,0x00,
         0xe8,0x91,0xd2,0x0d,0x00,0x8b,0xf8,0x85,0xff,0x0f,0x84,0xcc,0x2c,0x00,0x00,
         0x8b,0x46,0x08,0xd9,0x40,0x04,0xe8,0x38,0x16,0x23,0x00,0x88,0x87,0x08,0x01,0x00,0x00});
    if (s.god_error.empty()) s.god_error = entry_error(s, kGetServerPlayerRva,
        {0x6a,0xff,0x68,0x28,0x62,0x72,0x00,0x64});
    s.interface_error = entry_error(s, kSaveInterfaceTestRva,
        {0x83,0x3d,0xc4,0x2c,0x7c,0x00,0x04,0x75});
    // The entry itself is resolved when main_menu runs (main_menu_entry):
    // the addon may install its hook after this first harness step.
    s.menu_error = s.interface_error;
    if (s.menu_error.empty()) s.menu_error = entry_error(s, kRequestMainMenuRva + 0x54,
        {0xc7,0x06,0x01,0x00,0x00,0x00,0x5e,0xc3});
    // Native strings are one pointer: construction writes [ECX], and
    // destruction releases [ECX]. The dialog accesses name at +0, slot at
    // +4 (00653ADA) and zone at +14 (00653C1F). No full game object is copied.
    s.saves_error = entry_error(s, kStringConstructRva,
        {0xa1,0x48,0x6e,0x79,0x00,0x53,0x8b,0x5c});
    if (s.saves_error.empty()) s.saves_error = entry_error(s, kStringDestroyRva,
        {0x56,0x8b,0xf1,0x8b,0x06,0x8d,0x48,0xf4});
    if (s.saves_error.empty()) s.saves_error = entry_error(s, kDialogFormNameRva,
        {0x6a,0xff,0x68,0x10,0xef,0x72,0x00,0x64});
    if (s.saves_error.empty()) s.saves_error = entry_error(s, kLoadPathOperandRva - 2,
        {0x8b,0x0d});
    if (s.saves_error.empty()) s.saves_error = entry_error(s, kSavePathCallRva + 5,
        {0x8b,0x08,0x8d,0x54,0x24,0x18});
    if (s.saves_error.empty()) {
        const auto* instruction = s.image + kSavePathCallRva;
        std::uintptr_t cell = 0;
        if (instruction[0] == 0xe8) {
            s.saves_error = entry_error(s, kSavePathCallRva, {0xe8,0x2d,0x30,0xf2,0xff});
            if (s.saves_error.empty()) s.saves_error = entry_error(s, 0x00178db0,
                {0xb8,0x60,0xf4,0x7a,0x00,0xc3});
            cell = reinterpret_cast<std::uintptr_t>(s.image + kWorkPathCellRva);
        } else if (instruction[0] == 0xb8 && bridge_supported_addon_image()) {
            // The fingerprinted addon replaces this CALL with MOV EAX,<path
            // cell>, and redirects the load path to the same cell. Its binary
            // patch writer supplies the operand; it is not a guessed RVA or a
            // pointer used as mutable-state identity. Preserve the installed patch.
            std::memcpy(&cell, instruction + 1, sizeof(cell));
        } else {
            s.saves_error = "save path at 0x00655D7E is not stock or the verified addon redirect";
        }
        std::uintptr_t load_cell = 0;
        std::memcpy(&load_cell, s.image + kLoadPathOperandRva, sizeof(load_cell));
        if (s.saves_error.empty() && (!cell || cell != load_cell))
            s.saves_error = "dialog and native loader save paths differ (0x00655D7E/0x0057AC4E)";
        if (s.saves_error.empty()) s.save_path_cell = reinterpret_cast<const char* const*>(cell);
    }
    // RequestLoad and 0057C700 are resolved when load_save runs
    // (load_entry_error): the addon may hook them after this first step.
    s.load_error = s.saves_error;
    if (s.load_error.empty()) s.load_error = entry_error(s, 0x0017cb08,
        {0xc7,0x06,0x00,0x00,0x00,0x00});
    if (s.load_error.empty()) s.load_error = entry_error(s, kCurrentZoneRva,
        {0x51,0x56,0xb9,0xb8,0xf5,0x7a,0x00});
}

// The scene thread (render_thread option) completes renderer verbs while the
// game thread steps the queue: every public entry that touches the queue
// holds this lock. Recursive: a public entry may reach another.
std::recursive_mutex& harness_lock() {
    static std::recursive_mutex value;
    return value;
}

// Scene thread completions (harness_complete_entry). The scene thread never
// takes harness_lock: the game thread may hold it across a game call that
// waits for the scene thread (a load, a world change). The game thread
// applies these the next time it steps the queue.
struct PendingCompletion { unsigned long id; bool ok; std::string detail; };
std::mutex& pending_completion_lock() {
    static std::mutex value;
    return value;
}
std::vector<PendingCompletion>& pending_completions() {
    static std::vector<PendingCompletion> value;
    return value;
}
void retire_front(bool ok, const std::string& detail);
// Game thread, harness_lock held.
void apply_pending_completions();

State& state() {
    static State value = [] {
        State s;
        char directory[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR", directory, MAX_PATH);
        if (!length || length >= MAX_PATH) return s;
        const DWORD attributes = GetFileAttributesA(directory);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            OutputDebugStringA("EID3D11 harness disabled: EI_RENDERER_HARNESS_DIR is not a folder\n");
            return s;
        }
        s.directory = directory;
        if (s.directory.back() != '\\' && s.directory.back() != '/') s.directory += '\\';
        s.image = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
        s.game_verified = s.image != nullptr && game_sites_ok(GameSiteGroup::harness);
        for (const auto& entry : kEntries)
            s.game_verified = s.game_verified &&
                std::memcmp(s.image + entry.rva, entry.bytes, sizeof(entry.bytes)) == 0;
        s.quit_verified = lia_harness() ? std::memcmp(s.image + kLiaQuitProgramRva, kLiaQuitProgramStock,
                                                       sizeof(kLiaQuitProgramStock)) == 0
                                         : s.game_verified;
        initialize_game_commands(s);
        s.enabled = true;
        OutputDebugStringA(s.game_verified ?
            "EID3D11 harness reading cmd.txt\n" :
            "EID3D11 harness reading cmd.txt; game entry bytes differ: camera/quit disabled\n");
        return s;
    }();
    return value;
}

template <typename T> T& field(std::uint8_t* object, std::size_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}

float world_hour(std::uint8_t* world) {
    const double hours = static_cast<double>(field<std::int32_t>(world, kWorldTicks)) *
        field<float>(world, kWorldTimeScale) + field<float>(world, kWorldTimeModifier);
    double hour = std::fmod(hours, 24.0);
    if (hour < 0.0) hour += 24.0;
    return static_cast<float>(hour);
}

std::string text_of(const char* pattern, ...) {
    char text[512]{};
    va_list arguments;
    va_start(arguments, pattern);
    std::vsnprintf(text, sizeof(text), pattern, arguments);
    va_end(arguments);
    return text;
}

// Game text in this Russian image/info.sav is CP1251. Harness text is UTF-8;
// filesystem paths use the same Windows ANSI code page as the game's file API.
std::string utf8_text(const std::string& input, UINT code_page = 1251) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(code_page, 0, input.c_str(), -1, nullptr, 0);
    if (!size) return {};
    std::vector<wchar_t> wide(static_cast<std::size_t>(size));
    MultiByteToWideChar(code_page, 0, input.c_str(), -1, wide.data(), size);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
    if (!bytes) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, result.data(), bytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool game_text(const std::string& input, UINT code_page, std::string& result) {
    if (code_page == CP_ACP) code_page = GetACP();
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        input.c_str(), -1, nullptr, 0);
    if (!size) return false;
    std::vector<wchar_t> wide(static_cast<std::size_t>(size));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, wide.data(), size);
    BOOL substituted = FALSE;
    const DWORD flags = code_page == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL* used_default = code_page == CP_UTF8 ? nullptr : &substituted;
    const int bytes = WideCharToMultiByte(code_page, flags, wide.data(), -1,
                                         nullptr, 0, nullptr, used_default);
    if (!bytes || substituted) return false;
    result.assign(static_cast<std::size_t>(bytes), '\0');
    if (!WideCharToMultiByte(code_page, flags, wide.data(), -1,
                             result.data(), bytes, nullptr, used_default) || substituted) return false;
    result.pop_back();
    return true;
}

struct GameString {
    const char* data = nullptr;
    explicit GameString(const char* text) {
        using Construct = void*(__thiscall*)(void*, const char*);
        reinterpret_cast<Construct>(state().image + kStringConstructRva)(this, text);
    }
    ~GameString() {
        using Destroy = void(__thiscall*)(void*);
        reinterpret_cast<Destroy>(state().image + kStringDestroyRva)(this);
    }
    GameString(const GameString&) = delete;
    GameString& operator=(const GameString&) = delete;
};
static_assert(sizeof(GameString) == 4, "native x86 CString argument storage");

std::string dialog_save_name(const std::string& slot, const std::string& zone) {
    // LiA: the dialog's name formatter is not ported; slots stay selectable by key.
    if (lia_harness()) return slot;
    // Only the three native string fields read/written by 00653A50 and
    // 00653BD0 are supplied. The intervening words are never accessed by them.
    struct NameArgument {
        GameString name;
        GameString slot;
        std::uint32_t unread[3]{};
        GameString zone;
        NameArgument(const char* path, const char* map) : name(""), slot(path), zone(map) {}
    } argument(slot.c_str(), zone.c_str());
    static_assert(offsetof(NameArgument, slot) == 4 && offsetof(NameArgument, zone) == 0x14,
                  "binary-confirmed FormName arguments");
    using FormName = void(__thiscall*)(void*, bool);
    reinterpret_cast<FormName>(state().image + kDialogFormNameRva)(&argument, true);
    return utf8_text(argument.name.data);
}

std::string current_zone() {
    if (lia_harness()) {
        char* data = nullptr;
        using GetZone = void(__thiscall*)(void*, char**);
        reinterpret_cast<GetZone>(state().image + kLiaCurrentZoneRva)(
            state().image + kLiaClientInterfaceRva, &data);
        std::string zone = data ? data : "";
        if (data) {
            auto* header = data - 0x10;
            if (InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(header + 0x0c), -1) - 1 <= 0) {
                void* manager = *reinterpret_cast<void**>(header);
                using FreeFn = void(__thiscall*)(void*, void*);
                reinterpret_cast<FreeFn>((*reinterpret_cast<void***>(manager))[1])(manager, header);
            }
        }
        return zone;
    }
    GameString zone("");
    // 00656350..0065637A uses this accessor to fill the save dialog's zone
    // from the authoritative player's party, and destroys the returned string.
    using GetZone = void*(__thiscall*)(void*, GameString*);
    reinterpret_cast<GetZone>(state().image + kCurrentZoneRva)(
        state().image + kClientInterfaceRva, &zone);
    return zone.data;
}

std::string json_string(const std::string& text) {
    std::string result = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) {
            char escape[7]{};
            std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
            result += escape;
        } else result += static_cast<char>(c);
    }
    return result + '"';
}

bool is_file(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string full_path(const std::string& path) {
    const DWORD size = GetFullPathNameA(path.c_str(), 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(size, '\0');
    const DWORD length = GetFullPathNameA(path.c_str(), size, result.data(), nullptr);
    if (!length || length >= size) return {};
    result.resize(length);
    return result;
}

struct SaveSlot {
    std::string slot, name, zone, error;
    std::uint32_t time = 0;
};

bool read_string(FILE* file, std::string& result) {
    for (;;) {
        const int c = std::fgetc(file);
        if (c == EOF) return false;
        if (!c) return true;
        result += static_cast<char>(c);
    }
}

void read_save_info(const std::string& directory, SaveSlot& slot) {
    FILE* raw = nullptr;
    if (fopen_s(&raw, (directory + slot.slot + "\\info.sav").c_str(), "rb") || !raw) {
        slot.error = "info.sav unavailable";
        return;
    }
    std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, &std::fclose);
    // 00655FF5..00656095: signature=111h, versions=1..3, four-byte game
    // time, NUL-terminated allod and zone, time32 for v2+, name for v3.
    // 004A52D0 verifies two four-byte header words; 00461190 reads strings
    // byte by byte through the terminating NUL. No save payload is loaded here.
    std::uint32_t header[2]{};
    float hour = 0.0f;
    std::string allod, name;
    if (std::fread(header, sizeof(header), 1, raw) != 1 || header[0] != 0x111 ||
        header[1] < 1 || header[1] > 3 || std::fread(&hour, sizeof(hour), 1, raw) != 1 ||
        !read_string(raw, allod) || !read_string(raw, slot.zone) ||
        (header[1] > 1 && std::fread(&slot.time, sizeof(slot.time), 1, raw) != 1) ||
        (header[1] > 2 && !read_string(raw, name))) {
        slot.error = "invalid or truncated info.sav (expected signature 0x111, version 1..3)";
        return;
    }
    slot.name = name.empty() ? dialog_save_name(slot.slot, slot.zone) : utf8_text(name);
    if (!is_file(directory + slot.slot + "\\scenario.sav")) slot.error = "scenario.sav unavailable";
}

bool enumerate_saves(std::string& directory, std::vector<SaveSlot>& slots, std::string& error) {
    State& s = state();
    if (!s.saves_error.empty()) { error = s.saves_error; return false; }
    if (!s.save_path_cell || !*s.save_path_cell) { error = "native save path is unavailable"; return false; }
    directory = std::string(*s.save_path_cell) + "saves\\";
    WIN32_FIND_DATAA found{};
    const HANDLE search = FindFirstFileA((directory + "save*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        error = text_of("save enumeration failed (Windows error %lu)", GetLastError());
        return false;
    }
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        SaveSlot slot;
        slot.slot = found.cFileName;
        read_save_info(directory, slot);
        slots.push_back(std::move(slot));
    } while (FindNextFileA(search, &found));
    const DWORD last_error = GetLastError();
    FindClose(search);
    if (last_error != ERROR_NO_MORE_FILES) {
        error = text_of("save enumeration failed (Windows error %lu)", last_error);
        return false;
    }
    std::sort(slots.begin(), slots.end(), [](const SaveSlot& a, const SaveSlot& b) {
        return a.time != b.time ? a.time > b.time : a.slot < b.slot;
    });
    return true;
}

bool list_saves(std::string& detail) {
    std::string directory;
    std::vector<SaveSlot> slots;
    if (!enumerate_saves(directory, slots, detail)) return false;
    detail = "{\"directory\":" + json_string(utf8_text(directory, CP_ACP)) + ",\"saves\":[";
    bool first = true;
    for (const auto& slot : slots) {
        if (!first) detail += ',';
        first = false;
        detail += "{\"slot\":" + json_string(utf8_text(slot.slot, CP_ACP)) +
            ",\"name\":" + json_string(slot.name) + ",\"error\":" + json_string(slot.error) + '}';
    }
    detail += "]}";
    return true;
}

bool select_save(const std::string& selector, SaveSlot& selected, std::string& detail) {
    std::string directory;
    std::vector<SaveSlot> slots;
    if (!enumerate_saves(directory, slots, detail)) return false;
    std::string path;
    game_text(selector, CP_ACP, path);
    std::replace(path.begin(), path.end(), '/', '\\');
    std::string relative = path;
    if (relative.size() >= 6 && _strnicmp(relative.c_str(), "saves\\", 6) == 0) relative.erase(0, 6);
    const std::string absolute = path.empty() ? "" : full_path(path);
    const std::string within_saves = relative.empty() ? "" : full_path(directory + relative);
    unsigned matches = 0;
    for (const auto& slot : slots) {
        bool match = _stricmp(path.c_str(), slot.slot.c_str()) == 0 ||
                     (!slot.name.empty() && selector == slot.name);
        const std::string folder = full_path(directory + slot.slot);
        if (!folder.empty() && (_stricmp(absolute.c_str(), folder.c_str()) == 0 ||
                               _stricmp(within_saves.c_str(), folder.c_str()) == 0)) match = true;
        for (const char* file : {"info.sav", "mission.sav", "scenario.sav"}) {
            const std::string candidate = folder + '\\' + file;
            if (is_file(candidate) && (_stricmp(absolute.c_str(), candidate.c_str()) == 0 ||
                                      _stricmp(within_saves.c_str(), candidate.c_str()) == 0)) match = true;
        }
        if (match) { selected = slot; ++matches; }
    }
    if (matches != 1) {
        detail = matches ? "ambiguous save name; use a slot from list_saves"
                         : "save name or slot file not found in the Load dialog directory; use list_saves";
        return false;
    }
    if (!selected.error.empty()) { detail = selected.slot + ": " + selected.error; return false; }
    return true;
}

// Appends the unwritten result lines in order. One that cannot be written
// yet stays for the next present instead of being lost.
void write_results() {
    State& s = state();
    if (s.unwritten_results.empty()) return;
    FILE* file = nullptr;
    const errno_t opened = fopen_s(&file, (s.directory + "results.txt").c_str(), "ab");
    if (opened || !file) {
        const unsigned long error = opened ? static_cast<unsigned long>(opened) : 1ul;
        if (error != s.result_file_error) {
            s.result_file_error = error;
            bridge_report_event("harness_result_file",
                text_of("results.txt not writable (errno %lu): results held until it is", error).c_str());
        }
        return;
    }
    s.result_file_error = 0;
    while (!s.unwritten_results.empty()) {
        const std::string& line = s.unwritten_results.front();
        if (std::fwrite(line.data(), 1, line.size(), file) != line.size()) break;
        s.unwritten_results.pop_front();
    }
    std::fclose(file);
}

void retire_front(bool ok, const std::string& detail) {
    State& s = state();
    if (s.queue.empty()) return;
    const Entry& entry = s.queue.front();
    s.unwritten_results.push_back(text_of("%lu\t%s\t", entry.id, ok ? "done" : "failed") +
                                  entry.line + "\t" + detail + "\n");
    s.queue.pop_front();
    write_results();
}
void apply_pending_completions() {
    std::vector<PendingCompletion> completions;
    {
        std::lock_guard<std::mutex> guard(pending_completion_lock());
        completions.swap(pending_completions());
    }
    State& s = state();
    for (const auto& completion : completions)
        if (!s.queue.empty() && s.queue.front().id == completion.id) retire_front(completion.ok, completion.detail);
}

void load_commands() {
    State& s = state();
    const std::string path = s.directory + "cmd.txt";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") || !file) return;
    std::deque<std::string> lines;
    std::string line;
    for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
        if (c == '\n') { lines.push_back(std::move(line)); line.clear(); }
        else line += static_cast<char>(c);
    }
    if (!line.empty()) lines.push_back(std::move(line));
    const bool read_ok = !std::ferror(file);
    std::fclose(file);
    if (!read_ok) return;
    // A file that cannot be deleted yet is read again next present, never run
    // twice. Named once per error code: a writer that keeps cmd.txt open
    // (no delete sharing) otherwise leaves the harness polling it silently.
    if (!DeleteFileA(path.c_str())) {
        const DWORD error = GetLastError();
        if (error != s.command_file_error) {
            s.command_file_error = error;
            bridge_report_event("harness_command_file",
                text_of("cmd.txt read but not deleted (error %lu): its commands wait until the writer "
                        "releases it", static_cast<unsigned long>(error)).c_str());
        }
        return;
    }
    s.command_file_error = 0;
    for (auto& line : lines) {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (harness_line_skipped(line)) continue;
        Entry entry;
        entry.id = s.next_id++;
        entry.command = parse_harness_command(line);
        entry.line = std::move(line);
        s.queue.push_back(std::move(entry));
    }
}

std::string camera_state() {
    State& s = state();
    std::uint8_t* camera = s.image + kCameraRva;
    std::uint8_t* world = s.image + kWorldClientRva;
    const float* q = &field<float>(camera, kCameraRotation);
    const float yaw = 2.0f * std::atan2(q[3], q[0]) * 180.0f / kPi;
    return text_of("camera %.3f %.3f %.3f %.3f %.3f anchor_z=%.3f hour=%.3f clock=%s",
                  field<float>(camera, kCameraAnchor), field<float>(camera, kCameraAnchor + 4),
                  yaw, field<float>(camera, kCameraPitch) * 180.0f / kPi,
                  field<float>(camera, kCameraDistance), field<float>(camera, kCameraAnchor + 8),
                  world_hour(world), s.clock_pinned ? "pinned" : "free");
}

void apply_camera(const HarnessCommand& command) {
    State& s = state();
    std::uint8_t* camera = s.image + kCameraRva;
    const float radians = kPi / 180.0f;
    // "-" (NaN): keep the current yaw (read before SetCameraAs) or anchor coordinate.
    const float* q = &field<float>(camera, kCameraRotation);
    const float yaw = std::isnan(command.number[2]) ? 2.0f * std::atan2(q[3], q[0]) * 180.0f / kPi : command.number[2];
    const float anchor[3] = {std::isnan(command.number[0]) ? field<float>(camera, kCameraAnchor) : command.number[0],
                             std::isnan(command.number[1]) ? field<float>(camera, kCameraAnchor + 4) : command.number[1],
                             field<float>(camera, kCameraAnchor + 8)};
    // SCameraShortcut as SetCameraAs reads it: distance, rotation (unused), pitch.
    const float shortcut[6] = {command.number[4], 1.0f, 0.0f, 0.0f, 0.0f,
                               command.number[3] * radians};
    reinterpret_cast<SetCameraAsFn>(s.image + kSetCameraAsRva)(camera, shortcut);
    field<std::uint32_t>(camera, kCameraFlags) &= ~kCameraJumpClearedFlag;
    reinterpret_cast<SetCarrierFn>(s.image + kSetCarrierRva)(camera, 0xffffffffu);
    reinterpret_cast<SetAnchorPointFn>(s.image + kSetAnchorPointRva)(camera, anchor);
    reinterpret_cast<InitRotateFn>(s.image + kInitRotateRva)(camera, yaw * radians);
}

void apply_clock(std::uint8_t* world) {
    State& s = state();
    if (!s.clock_saved) {
        s.saved_scale = field<float>(world, kWorldTimeScale);
        s.saved_modifier = field<float>(world, kWorldTimeModifier);
        s.clock_saved = true;
    }
    field<float>(world, kWorldTimeScale) = 0.0f;
    field<float>(world, kWorldTimeModifier) = s.clock_hour;
}

void release_clock(std::uint8_t* world) {
    State& s = state();
    if (s.clock_saved) {
        field<float>(world, kWorldTimeScale) = s.saved_scale;
        field<float>(world, kWorldTimeModifier) = s.saved_modifier;
    }
    s.clock_pinned = s.clock_saved = false;
}

// A load entry as a game caller reaches it: stock, or the addon's verified
// hook chain (addon_hook_chain.h) with the body past its patch still stock.
// Empty, or the reason for any other bytes.
std::string load_entry_error(const char* name, std::uintptr_t rva,
                             const std::uint8_t* stock, std::size_t stock_bytes) {
    const State& s = state();
    const EntryResolution owner = resolve_entry_owner(s.image + rva, stock,
        stock_bytes < 16 ? stock_bytes : 16, 5, &addon_module_contains);
    if (owner.owner == EntryOwner::rejected)
        return text_of("%s entry (game.exe+0x%06lX) is neither stock nor a verified addon "
                       "hook chain: %s", name, static_cast<unsigned long>(rva),
                       owner.reason ? owner.reason : "unknown");
    const std::size_t patched = owner.owner == EntryOwner::stock ? 0 : owner.addon_patch_bytes;
    if (std::memcmp(s.image + rva + patched, stock + patched, stock_bytes - patched) != 0)
        return text_of("%s body (game.exe+0x%06lX) differs past the entry", name,
                       static_cast<unsigned long>(rva));
    return {};
}

// Runs at present, not at a world Draw: a modal screen (the game-over box,
// a dialog) stops world frames, yet its own Load button requests exactly this
// load, and 0057C700 destroys every screen before it loads. The request only
// queues the action consumed between frames. Returns false with the reason.
bool request_load(const HarnessCommand& command, std::string& detail) {
    State& s = state();
    // LiA's entries were checked with the rest at initialization (no addon).
    if (!lia_harness()) {
        detail = load_entry_error("RequestLoad", kRequestLoadRva, kRequestLoadStock, sizeof(kRequestLoadStock));
        if (detail.empty()) detail = load_entry_error("load transition", kLoadTransitionRva,
            kLoadTransitionStock, sizeof(kLoadTransitionStock));
        if (!detail.empty()) return false;
    }
    const auto lifecycle = lifecycle_observer().snapshot();
    if (!lifecycle.observer_active || !lifecycle.hooks_installed || lifecycle.fail_closed) {
        detail = "verified world lifecycle observer is unavailable; cannot establish load completion";
        return false;
    }
    SaveSlot slot;
    if (!select_save(command.name, slot, detail)) return false;
    auto* connector = s.image + connector_rva();
    // The action stays set until 0057C700 finishes the transition, so this
    // also refuses a request issued from a frame presented during loading.
    if (field<std::uint32_t>(connector, 0) != 0) {
        detail = "game already has a pending world transition";
        return false;
    }
    // Saved clock values belong to the previous map; do not restore them in
    // the newly loaded client or leave its time scale pinned to zero.
    release_clock(s.image + kWorldClientRva);
    s.load_slot = slot.slot;
    s.load_zone = slot.zone;
    s.load_wait = {};
    s.load_wait.request_epoch = bridge_current_world_epoch();
    reinterpret_cast<RequestLoadFn>(s.image + (lia_harness() ? kLiaRequestLoadRva : kRequestLoadRva))(
        connector, slot.slot.c_str(), false);
    s.load_waiting = true;
    s.world_done = false;
    s.load_started_ms = GetTickCount64();
    return true;
}

// ReqGoMainMenu as a game caller reaches it: the stock entry, or the
// addon's verified hook chain at it (addon_hook_chain.h). Null with the
// reason for any other patch.
RequestMainMenuFn main_menu_entry(std::string& error) {
    const State& s = state();
    const EntryResolution owner = resolve_entry_owner(s.image + kRequestMainMenuRva,
        kRequestMainMenuStock, sizeof(kRequestMainMenuStock), 5, &addon_module_contains);
    if (owner.owner == EntryOwner::rejected) {
        error = text_of("ReqGoMainMenu entry (game.exe+0x181660) is neither stock nor a verified "
                        "addon hook chain: %s", owner.reason ? owner.reason : "unknown");
        return nullptr;
    }
    return reinterpret_cast<RequestMainMenuFn>(s.image + kRequestMainMenuRva);
}

std::uint32_t server_interface() {
    return field<std::uint32_t>(state().image, lia_harness() ? kLiaServerInterfaceRva : kServerInterfaceRva);
}

bool on_travel_map() {
    const State& s = state();
    return s.enabled && s.interface_error.empty() && server_interface() == kInterfaceTravelMap;
}

bool mission_paused() {
    return reinterpret_cast<IsPauseFn>(state().image + kIsMissionPauseRva)(state().image + kInterfaceRva);
}

bool server_paused() {
    return reinterpret_cast<IsPauseFn>(state().image + kIsServerPauseRva)(state().image + kInterfaceRva);
}

std::string pause_state() {
    const bool mission = mission_paused(), server = server_paused();
    return text_of("paused=%s (mission flag %d, server flag %d)",
                   mission && server ? "on" : !mission && !server ? "off" : "mixed",
                   mission ? 1 : 0, server ? 1 : 0);
}

// At a Draw of the static world: the KA_PAUSE setter with the wanted value.
void request_pause(bool want) {
    State& s = state();
    std::uint8_t* world = s.image + kWorldClientRva;
    s.world_ok = true;
    if (mission_paused() == want && server_paused() == want) { s.world_detail = "already set"; return; }
    // CWorldClient::CombatPause dereferences these two without a check.
    const auto player = field<std::uint32_t>(world, kWorldPlayer);
    if (!player || !field<std::uint32_t>(reinterpret_cast<std::uint8_t*>(player), kPlayerSpecific)) {
        s.world_ok = false;
        s.world_detail = "world client has no player; the game's pause is unavailable here";
        return;
    }
    reinterpret_cast<SetPauseFn>(s.image + kMissionPauseRva)(s.image + kInterfaceRva, want);
    s.world_detail = "requested";
}

void observe_loaded_frame() {
    State& s = state();
    if (!s.load_waiting) return;
    const auto epoch = bridge_current_world_epoch();
    const auto completed_draws = bridge_world_draw_completion_counter();
    if (!harness_load_completed(s.load_wait, epoch, completed_draws,
            field<std::uint32_t>(s.image + connector_rva(), 0) != 0)) return;
    // An old/menu frame must not satisfy load_save. Read the authoritative
    // party's zone after the new epoch's successful Draw, not from a pointer
    // identity or from the mere existence of a CWorldClient.
    const auto zone = current_zone();
    s.world_ok = _stricmp(zone.c_str(), s.load_zone.c_str()) == 0;
    s.loaded_zone_empty = zone.empty();
    s.world_detail = s.world_ok
        ? text_of("slot=%s zone=%s world_epoch=%llu completed_world_frame=%llu",
                  s.load_slot.c_str(), zone.c_str(), static_cast<unsigned long long>(epoch),
                  static_cast<unsigned long long>(completed_draws))
        : "new world zone " + json_string(utf8_text(zone)) + " differs from save zone " +
          json_string(utf8_text(s.load_zone));
    s.load_waiting = false;
    s.world_done = true;
}

// The route banner (note verb), shown by the overlay and in the game window's
// title so a watcher sees which route and step is running.
std::string& banner_text() { static std::string text; return text; }

BOOL CALLBACK title_window(HWND window, LPARAM text) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(window, WM_SETTEXT, 0, text, SMTO_ABORTIFHUNG, 100, &ignored);
    return FALSE;
}

void set_banner(const std::string& note) {
    std::string& text = banner_text();
    text = note;
    for (char& c : text) if (c == '|') c = '\n';
    std::string title = "EI HARNESS - " + note;
    const std::wstring wide = [&] {
        std::wstring w(title.size(), L'\0');
        w.resize(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
                                                              w.data(), static_cast<int>(w.size()))));
        return w;
    }();
    EnumWindows(title_window, reinterpret_cast<LPARAM>(wide.c_str()));
}

} // namespace

std::string harness_banner_text() {
    if (!state().enabled) return {};
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    return banner_text();
}

bool harness_enabled() noexcept { return state().enabled; }

bool harness_on_travel_map() noexcept { return on_travel_map(); }

const std::string& harness_directory() noexcept { return state().directory; }

unsigned harness_camera_path_steps() noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    State& s = state();
    return !s.queue.empty() && s.queue.front().command.verb == HarnessVerb::camera_path ? s.queue.front().progress : 0u;
}

void harness_complete(bool ok, const std::string& detail) noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    retire_front(ok, detail);
}

bool harness_present_step_copy(HarnessCommand& command, unsigned long& entry_id,
                               unsigned& camera_steps, bool& on_travel_map_now) noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    try { apply_pending_completions(); } catch (...) {}
    entry_id = 0;
    camera_steps = 0;
    on_travel_map_now = false;
    const HarnessCommand* step = harness_present_step();
    if (!step) return false;
    State& s = state();
    if (s.queue.empty()) return false;
    try {
        command = *step;
    } catch (...) {
        return false;
    }
    entry_id = s.queue.front().id;
    camera_steps = harness_camera_path_steps();
    on_travel_map_now = on_travel_map();
    return true;
}

void harness_complete_entry(unsigned long entry_id, bool ok, const std::string& detail) noexcept {
    try {
        std::lock_guard<std::mutex> guard(pending_completion_lock());
        pending_completions().push_back({entry_id, ok, detail});
    } catch (...) {}
}


const HarnessCommand* harness_present_step() noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    State& s = state();
    if (!s.enabled) return nullptr;
    write_results();
    if (s.queue.empty()) load_commands();
    while (!s.queue.empty()) {
        Entry& entry = s.queue.front();
        const HarnessCommand& command = entry.command;
        switch (command.verb) {
        case HarnessVerb::invalid:
            retire_front(false, command.error);
            continue;
        case HarnessVerb::wait:
            if (++entry.progress < command.frames) return nullptr;
            retire_front(true, text_of("%u presents", command.frames));
            continue;
        case HarnessVerb::state:
            if (!s.game_verified) { retire_front(false, "unsupported game image"); continue; }
            retire_front(true, camera_state());
            continue;
        case HarnessVerb::note:
            set_banner(command.name);
            retire_front(true, "banner set");
            continue;
        case HarnessVerb::dump_units: {
            static HANDLE request = CreateEventA(nullptr, FALSE, FALSE, "Local\\EID3D11_DumpUnits");
            const bool ok = request && SetEvent(request);
            retire_front(ok, ok ? "eid3d11_units.txt next to game.exe on the next world frame"
                                : "event unavailable");
            continue;
        }
        case HarnessVerb::quit:
            if (!s.quit_verified) { retire_front(false, "unsupported game image"); continue; }
        {
            // QuitProgram only posts WM_QUIT, and only when the game allows
            // quitting; report its answer instead of assuming it.
            const bool posted = reinterpret_cast<QuitProgramFn>(
                s.image + (lia_harness() ? kLiaQuitProgramRva : kQuitProgramRva))(0);
            retire_front(posted, posted ? "QuitProgram(0) posted WM_QUIT"
                                        : "QuitProgram(0) refused by the game");
            if (posted) s.queue.clear();
            continue;
        }
        case HarnessVerb::load_save: {
            if (!s.load_error.empty()) { retire_front(false, s.load_error); continue; }
            if (!entry.posted) {
                if (!entry.progress++) s.load_started_ms = GetTickCount64();
                // Shortly after launch the game's own transition into the
                // main menu is still pending, and the game refuses a load
                // until it finishes: wait for it instead of failing.
                if (field<std::uint32_t>(s.image + connector_rva(), 0) != 0) {
                    if (!harness_timed_out(s.load_started_ms, GetTickCount64(), kHarnessLoadTimeoutSeconds))
                        return nullptr;
                    retire_front(false, text_of("game transition still pending after %u s",
                                                kHarnessLoadTimeoutSeconds));
                    continue;
                }
                entry.posted = true;
                std::string detail;
                if (!request_load(command, detail)) { retire_front(false, detail); continue; }
            }
            observe_loaded_frame();
            if (s.world_done) {
                s.world_done = false;
                if (harness_load_retry(s.world_ok, s.loaded_zone_empty, entry.retried)) {
                    entry.posted = false;
                    return nullptr;
                }
                retire_front(s.world_ok, s.world_detail);
                continue;
            }
            // The load finished (new epoch, no transition) into the travel
            // map: no world frame will come, so do not wait out the limit.
            if (s.load_waiting && on_travel_map() &&
                bridge_current_world_epoch() != s.load_wait.request_epoch &&
                field<std::uint32_t>(s.image + connector_rva(), 0) == 0) {
                s.load_waiting = false;
                retire_front(false, "opened the world map (no mission in save); slot=" + s.load_slot);
                continue;
            }
            // Loading blocks presentation, so the limit is wall-clock time.
            if (harness_timed_out(s.load_started_ms, GetTickCount64(), kHarnessLoadTimeoutSeconds)) {
                s.load_waiting = false;
                retire_front(false, text_of("no completed world frame in a new world epoch within %u s "
                    "(request epoch %llu, current epoch %llu, transition %s)", kHarnessLoadTimeoutSeconds,
                    static_cast<unsigned long long>(s.load_wait.request_epoch),
                    static_cast<unsigned long long>(bridge_current_world_epoch()),
                    field<std::uint32_t>(s.image + connector_rva(), 0) ? "pending" : "none"));
                continue;
            }
            return nullptr;
        }
        case HarnessVerb::main_menu: {
            if (!s.menu_error.empty()) { retire_front(false, s.menu_error); continue; }
            auto* connector = s.image + kConnectorRva;
            if (!entry.posted) {
                if (field<std::uint32_t>(connector, 0) != 0) {
                    retire_front(false, "game already has a pending world transition");
                    continue;
                }
                std::string error;
                const RequestMainMenuFn request = main_menu_entry(error);
                if (!request) { retire_front(false, error); continue; }
                entry.posted = true;
                release_clock(s.image + kWorldClientRva);
                request(connector);
                s.menu_started_ms = GetTickCount64();
                return nullptr; // consumed between frames
            }
            if (field<std::uint32_t>(connector, 0) == 0 && server_interface() == kInterfaceMainMenu) {
                retire_front(true, text_of("main menu (world epoch %llu)",
                    static_cast<unsigned long long>(bridge_current_world_epoch())));
                continue;
            }
            if (harness_timed_out(s.menu_started_ms, GetTickCount64(), kHarnessLoadTimeoutSeconds)) {
                retire_front(false, text_of("main menu not reached within %u s (interface %u, transition %s)",
                    kHarnessLoadTimeoutSeconds, server_interface(),
                    field<std::uint32_t>(connector, 0) ? "pending" : "none"));
                continue;
            }
            return nullptr;
        }
        case HarnessVerb::list_saves:
        case HarnessVerb::console: {
            const std::string& error = command.verb == HarnessVerb::console ? s.console_error : s.saves_error;
            if (!error.empty()) { retire_front(false, error); continue; }
            if (!entry.posted) {
                entry.posted = true;
                s.world_pending = true;
                s.world_done = false;
            }
            if (s.world_done) {
                s.world_done = false;
                retire_front(s.world_ok, s.world_detail);
                continue;
            }
            if (on_travel_map()) { s.world_pending = false; retire_front(false, kTravelMapDetail); continue; }
            if (++entry.progress > kWorldCommandTimeoutPresents) {
                s.world_pending = false;
                retire_front(false, "no world frame within 600 presents");
                continue;
            }
            return nullptr;
        }
        case HarnessVerb::pause: {
            if (!s.pause_error.empty()) { retire_front(false, s.pause_error); continue; }
            const bool want = command.frames != 0;
            if (!entry.posted) {
                entry.posted = true;
                s.world_pending = true;
                s.world_done = false;
            }
            if (!s.world_done) {
                if (on_travel_map()) { s.world_pending = false; retire_front(false, kTravelMapDetail); continue; }
                if (++entry.progress > kWorldCommandTimeoutPresents) {
                    s.world_pending = false;
                    retire_front(false, "no world frame within 600 presents (a modal screen such as "
                                        "the game-over box or a dialog stops world frames)");
                    continue;
                }
                return nullptr;
            }
            // The setter ran at a world frame; the client flag follows the
            // server's next update, so wait for both flags to agree.
            if (!s.world_ok) { s.world_done = false; retire_front(false, s.world_detail); continue; }
            if (mission_paused() == want && server_paused() == want) {
                s.world_done = false;
                retire_front(true, pause_state() + "; " + s.world_detail);
                continue;
            }
            if (++entry.progress > kWorldCommandTimeoutPresents) {
                s.world_done = false;
                retire_front(false, "pause request not reflected within 600 presents: " + pause_state());
                continue;
            }
            return nullptr;
        }
        case HarnessVerb::camera_path:
            // Temporary (camera-movement frame cost): one step at each world
            // frame (harness_world_frame); the renderer writes each present's
            // timing while it runs.
            if (!s.game_verified) { retire_front(false, "unsupported game image"); continue; }
            if (on_travel_map()) { s.world_pending = false; retire_front(false, kTravelMapDetail); continue; }
            if (entry.posted && s.world_done && entry.progress >= command.frames) {
                s.world_done = false;
                retire_front(s.world_ok, s.world_detail);
                continue;
            }
            entry.posted = true;
            s.world_pending = true;s.world_done = false;
            return &command;
        case HarnessVerb::camera:
        case HarnessVerb::clock_pin:
        case HarnessVerb::clock_release:
        case HarnessVerb::godmode:
            if (command.verb == HarnessVerb::godmode && !s.god_error.empty()) {
                retire_front(false, s.god_error);
                continue;
            }
            if (!s.game_verified && !(lia_harness() && command.verb == HarnessVerb::godmode)) {
                retire_front(false, "unsupported game image");
                continue;
            }
            if (!entry.posted) {
                entry.posted = true;
                s.world_pending = true;
                s.world_done = false;
            }
            if (s.world_done) {
                s.world_done = false;
                retire_front(s.world_ok, s.world_detail);
                continue;
            }
            if (on_travel_map()) { s.world_pending = false; retire_front(false, kTravelMapDetail); continue; }
            if (++entry.progress > kWorldCommandTimeoutPresents) {
                s.world_pending = false;
                retire_front(false, "no world frame within 600 presents");
                continue;
            }
            return nullptr;
        default:
            return &command;
        }
    }
    return nullptr;
}

// The game's projection builders read the camera far plane from
// theCamera +740. Keep projection clipping beyond the owned draw distance;
// the renderer still owns visibility. This input write is not a projection rebuild.
namespace {
constexpr float kGameProjectionFarClip = 10000.f;
float g_game_effect_distance = 0.f;

// The server's send ranges (kMapObjectRangeRva, kEffectRangeRva) follow the
// renderer's draw distance, so a torch, campfire, map particle source, light
// or bolt on screen is not removed on the client. They are measured in 2D from
// the camera's observation point, which lies within fDistance of the eye, so
// back clip + fDistance covers everything inside the far clip. Never below the
// stock 100 m. The page is made writable once; each value is written only
// while it still holds what the game or this code last stored there.
void game_visual_range_world_frame(std::uint8_t* image) noexcept {
    static int usable = -1;
    static float written = kStockVisualRange;
    float* const ranges[2] = {reinterpret_cast<float*>(image + kMapObjectRangeRva),
                              reinterpret_cast<float*>(image + kEffectRangeRva)};
    if (usable < 0) {
        static const std::uint8_t get_range[] = {0xd9,0x05,0xac,0x33,0x74,0x00,0xc3};
        static const std::uint8_t range_load[] = {0xd9,0x05,0x1c,0xf9,0x73,0x00};
        usable = std::memcmp(image + kEffectGetRangeRva, get_range, sizeof(get_range)) == 0 &&
                 std::memcmp(image + kMapObjectRangeLoadRva, range_load, sizeof(range_load)) == 0 &&
                 std::memcmp(image + kMapObjectRangeLoad2Rva, range_load, sizeof(range_load)) == 0 &&
                 *ranges[0] == kStockVisualRange && *ranges[1] == kStockVisualRange;
        DWORD old = 0;
        for (float* range : ranges)
            if (usable && !VirtualProtect(range, sizeof(float), PAGE_READWRITE, &old)) usable = 0;
        char detail[96];
        std::snprintf(detail, sizeof(detail), usable ? "server send ranges follow the draw distance"
                                                     : "rejected: send range code or values differ");
        bridge_report_event("game_visual_range", detail);
    }
    if (!usable) return;
    // F10 effect_distance, measured from the camera's observation point like
    // the stock 100 m: 100 behaves exactly as the game.
    const float range = std::max(kStockVisualRange, g_game_effect_distance);
    if (range == written) return;
    if (*ranges[0] != written || *ranges[1] != written) {
        usable = 0;
        bridge_report_event("game_visual_range", "stopped: another writer changed a send range");
        return;
    }
    *ranges[0] = *ranges[1] = written = range;
}
}  // namespace
void set_game_effect_distance(float distance) noexcept { g_game_effect_distance = distance; }
void game_back_clip_world_frame(void* world_client) noexcept {
    static int usable = -1;
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    const bool lia = running_game_sites().report.engine == GameEngine::lia;
    if (!image || world_client != image + (lia ? kLiaWorldClientRva : kWorldClientRva)) return;
    if (usable < 0) usable = (lia ? bridge_lia_back_clip_contract_matches()
                                  : game_sites_ok(GameSiteGroup::harness)) ? 1 : 0;
    if (!usable) return;
    float& back = field<float>(image + (lia ? kLiaCameraRva : kCameraRva), 740);
    static bool logged = false;
    if (!logged) {
        logged = true;
        char detail[96];
        std::snprintf(detail, sizeof(detail), "first world frame: camera back clip %.1f, wanted %.1f", back, kGameProjectionFarClip);
        bridge_report_event("game_back_clip", detail);
    }
    if (std::isfinite(back) && back > 0.f && back != kGameProjectionFarClip) back = kGameProjectionFarClip;
    if (lia) {
        static bool ranges_reported = false;
        if (!ranges_reported) {
            ranges_reported = true;
            bridge_report_event("game_visual_range", "LiA send ranges not established; native values retained");
        }
    } else game_visual_range_world_frame(image);
}

// The pause verb's contract (same sites, all in the harness group; same Draw
// entry; same player checks before CWorldClient::CombatPause), driven by the
// renderer's load reveal instead of cmd.txt. The setter only requests the
// pause: the server applies it at its next update and the client flag follows.
// A pause this function set is released when the hold ends, and after 6 s
// whatever the renderer does, so it can never leave the game paused.
void load_pause_world_frame(void* world_client, bool hold, std::uint64_t epoch,
                            bool single_player_mission) noexcept {
    static int usable = -1;
    static bool ours = false;
    static std::uint64_t decided_epoch = 0;
    static ULONGLONG paused_at = 0;
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    if (!image || world_client != image + kWorldClientRva) return;
    if (usable < 0) usable = game_sites_ok(GameSiteGroup::harness) ? 1 : 0;
    if (!usable) return;
    const auto paused = [&] {
        return reinterpret_cast<IsPauseFn>(image + kIsMissionPauseRva)(image + kInterfaceRva) ||
               reinterpret_cast<IsPauseFn>(image + kIsServerPauseRva)(image + kInterfaceRva);
    };
    const auto set = [&](bool want) {
        // CWorldClient::CombatPause dereferences these two without a check.
        std::uint8_t* world = image + kWorldClientRva;
        const auto player = field<std::uint32_t>(world, kWorldPlayer);
        if (!player || !field<std::uint32_t>(reinterpret_cast<std::uint8_t*>(player), kPlayerSpecific)) return false;
        reinterpret_cast<SetPauseFn>(image + kMissionPauseRva)(image + kInterfaceRva, want);
        return true;
    };
    const ULONGLONG now = GetTickCount64();
    if (ours && now - paused_at > 6000) hold = false;
    if (hold && single_player_mission && epoch && epoch != decided_epoch) {
        if (ours) {               // a new load while still holding: keep our pause
            decided_epoch = epoch; paused_at = now;
            if (!paused()) set(true);
            return;
        }
        if (paused()) {           // the player's (or the save's) pause: not ours
            decided_epoch = epoch;
            bridge_report_event("load_pause", "game already paused; left as it is");
            return;
        }
        if (set(true)) {
            decided_epoch = epoch; ours = true; paused_at = now;
            bridge_report_event("load_pause", "paused while the loading picture is held");
        }
        return;
    }
    if (!hold && ours) {
        ours = false;
        const bool resumed = paused() && set(false);
        bridge_report_event("load_pause", resumed ? "resumed at reveal" : "already unpaused at reveal");
    }
}

// The game builds its projection (and so its frustum culling) from
// theCamera.dvBackClip in CVisualizatorD3D::Create, right after it
// creates its DirectDraw device through DirectDrawCreateEx. Set the back clip to
// a high fixed projection range there, before the game reads it.
void apply_initial_game_back_clip() noexcept {
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    if (!image) return;
    const bool lia = running_game_sites().report.engine == GameEngine::lia;
    if (!(lia ? bridge_lia_back_clip_contract_matches() : game_sites_ok(GameSiteGroup::harness))) {
        bridge_report_event("game_back_clip", "skipped: site check not passed at DirectDraw creation");
        return;
    }
    float& back = field<float>(image + (lia ? kLiaCameraRva : kCameraRva), 740);
    const float before = back;
    if (std::isfinite(back) && back > 0.f && back != kGameProjectionFarClip) back = kGameProjectionFarClip;
    char detail[128];
    std::snprintf(detail, sizeof(detail), "camera back clip %.1f -> %.1f at DirectDraw creation (fixed projection range)", before, back);
    bridge_report_event("game_back_clip", detail);
}

// Debug lightning (F10 Diagnostics button, no harness needed): strikes at the
// camera's ground focus point (the camera anchor) exactly as the rain does,
// with InvokeLightning's own script handed to the server's scriptTemp
// (kScriptLoadTextRva): a point light, the strike sound and two bolts, each
// deleted by the script one Sleep later. The console cannot do this: it runs
// script text only after an '@' and the cheat word. Script ids 7/8 keep a real
// rain strike (ids 1/2) intact.
namespace { bool g_debug_lightning_requested = false; }
void request_debug_lightning() noexcept { g_debug_lightning_requested = true; }

void debug_lightning_world_frame(void* world_client) noexcept {
    static int usable = -1;
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    if (!g_debug_lightning_requested) return;
    if (!image || world_client != image + kWorldClientRva) {
        bridge_report_event("debug_lightning", "waiting: no world frame (open a map)"); return; }
    if (usable < 0) {
        static const std::uint8_t load_text[] = {0x6a,0xff,0x68,0xdc,0xa5,0x71,0x00,0x64};
        static const std::uint8_t weather_call[] = {0x6a,0x01,0x51,0xb9,0xcc,0x29,0x7c,0x00,0xe8};
        static const std::uint8_t string_construct[] = {0xa1,0x48,0x6e,0x79,0x00,0x53,0x8b,0x5c};
        static const std::uint8_t string_destroy[] = {0x56,0x8b,0xf1,0x8b,0x06,0x8d,0x48,0xf4};
        // The addon detours the script loader's entry with a 5-byte jump; the
        // game's own weather call goes through that chain, and so do we. The
        // weather call site and its rel32 target prove the function's identity.
        const std::uint8_t* entry = image + kScriptLoadTextRva;
        const std::uint8_t* call = image + kWeatherLoadTextCallRva;
        std::int32_t rel = 0;std::memcpy(&rel, call + sizeof(weather_call), 4);
        usable = game_sites_ok(GameSiteGroup::harness) &&
                 (std::memcmp(entry, load_text, sizeof(load_text)) == 0 || entry[0] == 0xe9) &&
                 std::memcmp(call, weather_call, sizeof(weather_call)) == 0 &&
                 call + sizeof(weather_call) + 4 + rel == entry &&
                 std::memcmp(image + kStringConstructRva, string_construct, sizeof(string_construct)) == 0 &&
                 std::memcmp(image + kStringDestroyRva, string_destroy, sizeof(string_destroy)) == 0;
    }
    g_debug_lightning_requested = false;
    if (!usable) { bridge_report_event("debug_lightning", "rejected: script entry bytes or harness sites differ"); return; }
    // The rain's geometry: a bolt to 5 m below the ground from 40 m above it,
    // offset a little sideways; the light at its middle.
    std::uint8_t* camera = image + kCameraRva;
    const float x = field<float>(camera, kCameraAnchor), y = field<float>(camera, kCameraAnchor + 4),
                z = field<float>(camera, kCameraAnchor + 8) - 5.f;
    const float fx = x + 1.5f, fy = y - 1.f, fz = z + 40.f;
    char script[640];
    std::snprintf(script, sizeof(script),
        "WorldScript(CreatePointLight(7,%f,%f,%f,80,255,255,255 ) "
        "CreateFX( %f,%f,%f,60,150,\"nature\\lightning\\%i.wav\" ) "
        "CreateLightning(7,%f,%f,%f,%f,%f,%f,-7)Sleep(1) DeleteLightning(7) Sleep(1)"
        "CreateLightning(8,%f,%f,%f,%f,%f,%f,-7)Sleep(1) DeleteLightning(8) DeletePointLight(7) )",
        (fx + x) * .5f, (fy + y) * .5f, (fz + z) * .5f, fx, fy, fz, 1,
        fx, fy, fz, x, y, z, fx, fy, fz, x, y, z);
    using Construct = void*(__thiscall*)(void*, const char*);
    using Destroy = void(__thiscall*)(void*);
    using LoadText = BOOL(__thiscall*)(void*, void*, BOOL);
    void* text = nullptr;  // a native CString is one pointer
    reinterpret_cast<Construct>(image + kStringConstructRva)(&text, script);
    const BOOL loaded = reinterpret_cast<LoadText>(image + kScriptLoadTextRva)(image + kServerScriptTempRva, &text, TRUE);
    reinterpret_cast<Destroy>(image + kStringDestroyRva)(&text);
    char detail[96];
    std::snprintf(detail, sizeof(detail), "%s: strike at %.1f %.1f %.1f", loaded ? "loaded" : "script rejected", x, y, z);
    bridge_report_event("debug_lightning", detail);
}

void harness_world_frame(void* world_client) noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    try { apply_pending_completions(); } catch (...) {}
    State& s = state();
    if (!s.enabled || !s.image) return;
    std::uint8_t* world = s.image + kWorldClientRva;
    // LiA: only the console and list_saves run at its world client's Draw
    // entry (the main menu's world draws through it too).
    const bool lia_world = lia_harness() && world_client == s.image + kLiaWorldClientRva;
    const bool supported_world = !lia_harness() && world_client == world;
    if (s.world_pending && !s.queue.empty()) {
        Entry& entry = s.queue.front();
        const HarnessCommand& command = entry.command;
        const bool lia_verb = command.verb == HarnessVerb::console || command.verb == HarnessVerb::list_saves ||
                              command.verb == HarnessVerb::godmode;
        s.world_pending = false;
        s.world_done = true;
        s.world_ok = supported_world || (lia_world && lia_verb);
        s.world_detail = lia_world && !lia_verb ? "not ported on LiA" :
                         "Draw target is not the static CWorldClient";
        if (supported_world && command.verb == HarnessVerb::camera_path) {
            // Temporary: the start pose at the first step, then the pose
            // interpolated linearly (yaw along the shorter arc).
            std::uint8_t* camera = s.image + kCameraRva;
            if (entry.progress == 0) {
                const float* q = &field<float>(camera, kCameraRotation);
                s.path_start[0] = field<float>(camera, kCameraAnchor);
                s.path_start[1] = field<float>(camera, kCameraAnchor + 4);
                s.path_start[2] = 2.0f * std::atan2(q[3], q[0]) * 180.0f / kPi;
                s.path_start[3] = field<float>(camera, kCameraPitch) * 180.0f / kPi;
                s.path_start[4] = field<float>(camera, kCameraDistance);
            }
            ++entry.progress;
            const float t = static_cast<float>(entry.progress) / static_cast<float>(command.frames);
            HarnessCommand step = command;
            for (int i = 0; i < 5; ++i) {
                float delta = command.number[i] - s.path_start[i];
                if (i == 2) delta = std::remainder(delta, 360.0f);
                step.number[i] = s.path_start[i] + delta * t;
            }
            apply_camera(step);
            s.world_detail = text_of("step %u/%u; ", entry.progress, command.frames) + camera_state();
        } else if (supported_world && command.verb == HarnessVerb::pause) {
            entry.progress = 0;
            request_pause(command.frames != 0);
        } else if ((supported_world || lia_world) && command.verb == HarnessVerb::godmode) {
            // Player 0 is the local party's server player (its party
            // variables are read through GetPlayer(0) as well).
            std::uint8_t* player = reinterpret_cast<GetServerPlayerFn>(
                s.image + (lia_world ? kLiaGetServerPlayerRva : kGetServerPlayerRva))(
                s.image + (lia_world ? kLiaServerRva : kServerRva), 0);
            s.world_ok = player != nullptr;
            if (player) {
                field<std::uint8_t>(player, kPlayerGod) = command.frames != 0 ? 1 : 0;
                s.world_detail = text_of("godmode=%s (player 0 flag %u)", command.frames ? "on" : "off",
                                         static_cast<unsigned>(field<std::uint8_t>(player, kPlayerGod)));
            } else s.world_detail = "server has no player 0";
        } else if ((supported_world || lia_world) && command.verb == HarnessVerb::list_saves) {
            s.world_ok = list_saves(s.world_detail);
        } else if ((supported_world || lia_world) && command.verb == HarnessVerb::console) {
            std::string text;
            s.world_ok = game_text(command.name, 1251, text);
            if (s.world_ok) {
                reinterpret_cast<ConsoleProcessFn>(
                    s.image + (lia_world ? kLiaConsoleProcessRva : kConsoleProcessRva))(text.c_str());
                // The native parser returns void; unknown commands and cheat
                // activation are reported by the game's console, not inferred here.
                s.world_detail = "dispatched to native console parser (no native success return)";
            } else s.world_detail = "console text is not valid UTF-8 or cannot be represented in game CP1251";
        } else if (supported_world && command.verb == HarnessVerb::camera) {
            apply_camera(command);
            s.world_detail = camera_state();
        } else if (supported_world && command.verb == HarnessVerb::clock_pin) {
            const float before = world_hour(world);
            s.clock_hour = command.number[0];
            s.clock_pinned = true;
            apply_clock(world);
            s.world_detail = text_of("hour %.3f (was %.3f); client clock only", s.clock_hour, before);
        } else if (supported_world && command.verb == HarnessVerb::clock_release) {
            const bool was_pinned = s.clock_pinned;
            release_clock(world);
            s.world_detail = was_pinned ?
                text_of("restored scale %.6f modifier %.3f; hour %.3f",
                       s.saved_scale, s.saved_modifier, world_hour(world)) : "clock was not pinned";
        }
    }
    if (s.clock_pinned && supported_world) apply_clock(world);
}

} // namespace owned
