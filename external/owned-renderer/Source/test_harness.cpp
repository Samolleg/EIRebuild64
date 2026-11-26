#include "test_harness.h"
#include "engine_bridge.h"
#include "addon_hook_chain.h"
#include "x64_remote_link.h"

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
#include <share.h>
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
// The current map (#292): CCamera's scroll and follow paths clamp the anchor
// to [0, width-1] x [0, height-1] of the map at 007AF738, width at +0x10C and
// height at +0x110 (0040FECC..0040FEDB, 0040FF7D). SetAnchorPoint does not
// clamp, and the 3D scene draw (0056AFE0) overruns its cell grid when the
// anchor is more than 100 m off the map, so the harness rejects such stops.
constexpr std::uintptr_t kCurrentMapRva = 0x003af738;
constexpr std::uintptr_t kScrollMapWidthSiteRva = 0x0000fecc;
constexpr std::uintptr_t kScrollMapHeightSiteRva = 0x0000ff7d;
constexpr std::size_t kMapWidth = 0x10c, kMapHeight = 0x110;
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
    {kScrollMapWidthSiteRva, {0x8b, 0x0d, 0x38, 0xf7, 0x7a, 0x00, 0xc7, 0x45}},
    {kScrollMapHeightSiteRva, {0x8b, 0x88, 0x10, 0x01, 0x00, 0x00, 0x89, 0x4d}},
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
    std::uint64_t started_ms = 0;
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
    bool hero_follow = false;
    std::uint64_t hero_epoch = 0;
    // Event expectations read the existing logs, delimited before load and
    // after the renderer reached the assertion. No logging hot-path hook.
    std::wstring event_bridge_path;
    std::string event_load_marker;
    std::uint64_t event_bridge_start = 0, event_epoch = 0;
    float clock_hour = 0.0f;
    float path_start[5]{}; // camera_path (temporary): the pose it starts from (camera order)
    // camera_unit: the newest world view (harness_note_world_view), counted;
    // the pivot being corrected, the view count when it was last applied,
    // the corrections applied and whether the command has its result.
    std::vector<HarnessUnitPosition> view_units;
    std::array<float, 16> view_clip{};
    std::uint64_t views_noted = 0, unit_applied_view = 0;
    float unit_anchor[2]{};
    float unit_distance = 0.0f; // the game camera distance being corrected
    std::int32_t unit_target_id = 0; // camera_unit @x,y: the unit it picked
    unsigned unit_step = 0;
    bool unit_finished = false;
    // drawall: DrawVisibleOnly as the first drawall on found it.
    bool drawall_saved = false;
    std::int32_t drawall_previous = 0;
    unsigned long command_file_error = 0; // the last cmd.txt delete failure named (0: none)
    // Result lines not yet in results.txt (another process held it without
    // write sharing): the driver waits on each line, so none may be dropped.
    std::deque<std::string> unwritten_results;
    unsigned long result_file_error = 0; // the last results.txt open failure named (0: none)
    bool clock_saved = false;
    float saved_scale = 0.0f, saved_modifier = 0.0f;
    // Outfitting screen (camp) verbs: hand-off to the camp Draw entry.
    bool camp_pending = false, camp_done = false, camp_ok = false;
    std::string camp_detail;
    std::uint64_t camp_frames = 0, camp_frames_at_open = 0, outfit_started_ms = 0;
    unsigned camp_idle_presents = 0;
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

bool event_expectation(HarnessVerb verb) {
    return verb == HarnessVerb::expect_event_absent || verb == HarnessVerb::expect_event_present;
}

void begin_event_window() {
    State& s = state();
    wchar_t root[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"EI_RENDERER_DEBUG_DIR", root, MAX_PATH);
    if (n && n < MAX_PATH) s.event_bridge_path = root;
    else {
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", root, MAX_PATH);
        s.event_bridge_path = n && n < MAX_PATH ? std::wstring(root) + L"\\EID3D11" : L".\\EID3D11-debug";
    }
    s.event_bridge_path += L"\\bridge.jsonl";
    // The load marker is appended after this size, so the window reader starts
    // here instead of rereading the whole session log. No file yet: 0.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    s.event_bridge_start = GetFileAttributesExW(s.event_bridge_path.c_str(), GetFileExInfoStandard, &info)
        ? (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow : 0;
    s.event_load_marker = text_of("%lu:%lu:%llu", GetCurrentProcessId(), s.queue.front().id, GetTickCount64());
    s.event_epoch = 0; // established only by this load's successful completion
    bridge_append_probe_event("harness_load_begin", s.event_load_marker.c_str());
}

// 0: the logger has not reached the barrier; 1: complete; -1: unavailable.
// Only complete records count. Exact event names, never a substring of detail.
// Reads bridge.jsonl from the size recorded before the load, in blocks.
int read_event_window(const std::wstring& path, std::uint64_t offset,
                      const std::string& load_marker, const std::string& barrier,
                      const std::string& wanted, unsigned& count, std::string& detail) {
    FILE* file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!file) {
        detail = "bridge.jsonl unavailable";
        return 0; // async first open / transient sharing conflict: bounded by the command
    }
    const auto close = std::unique_ptr<FILE, decltype(&std::fclose)>(file, &std::fclose);
    if (_fseeki64(file, 0, SEEK_END) || _ftelli64(file) < static_cast<__int64>(offset) ||
        _fseeki64(file, static_cast<__int64>(offset), SEEK_SET)) {
        detail = "event log truncated since load"; return -1;
    }
    const std::string start = "\"event\":\"harness_load_begin\",\"detail\":\"" + load_marker + "\"";
    const std::string stop = "\"event\":\"harness_event_barrier\",\"detail\":\"" + barrier + "\"";
    const std::string key = "{\"event\":\"" + wanted + "\",";
    bool in_window = false;
    std::string line;
    std::vector<char> block(64 * 1024);
    count = 0;
    for (std::size_t got; (got = std::fread(block.data(), 1, block.size(), file)) != 0;) {
        for (std::size_t begin = 0; begin < got;) {
            const auto* newline = static_cast<const char*>(std::memchr(block.data() + begin, '\n', got - begin));
            const std::size_t end = newline ? std::size_t(newline - block.data()) : got;
            line.append(block.data() + begin, end - begin);
            begin = end + 1;
            if (!newline) break; // partial record: continued by the next block
            if (!in_window) in_window = line.find(start) != std::string::npos;
            else if (line.find(stop) != std::string::npos) return 1;
            else if (line.compare(0, key.size(), key) == 0) ++count;
            line.clear();
        }
    }
    detail = std::ferror(file) ? "event log read failed" :
        !in_window ? "load marker not found in bridge.jsonl" : "event log barrier not written yet";
    return std::ferror(file) ? -1 : 0;
}

bool finish_event_expectation(const Entry& entry, bool& ok, std::string& detail) {
    const State& s = state();
    const auto& command = entry.command;
    unsigned count = 0;
    const int result = read_event_window(s.event_bridge_path, s.event_bridge_start, s.event_load_marker,
                                         command.value + ":game", command.name, count, detail);
    if (result == 0 && !harness_timed_out(entry.started_ms, GetTickCount64(), 5)) return false;
    ok = false;
    // On timeout the reader's specific reason (marker missing, barrier not written) is kept.
    if (result != 1) { detail = "expect " + command.name + ": " + detail; return true; }
    ok = command.verb == HarnessVerb::expect_event_present ? count != 0 : count == 0;
    detail = text_of("event %s: %u occurrence(s) since load; expected %s", command.name.c_str(), count,
                     command.verb == HarnessVerb::expect_event_present ? "present" : "absent");
    return true;
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
    // The renderer acknowledges its log barrier; only the game-side log
    // reader produces this expectation's result, once both logs reach it.
    if (event_expectation(s.queue.front().command.verb) && s.queue.front().progress == 1 &&
        ok && detail == "event barrier emitted") {
        bridge_append_probe_event("harness_event_barrier", (s.queue.front().command.value + ":game").c_str());
        s.queue.front().progress = 2;
        return;
    }
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

// state (#317): the client's interface screen stack, bottom to top, by each
// screen's primary vtable (theInterface 007C7D50, stack begin/end at +34/+38:
// the outfitting verbs' verified read, GetCurrentScreen 005F7590). Names
// for the game's own screens; another table (an addon's screen) as its RVA.
int screen_stack_rvas(std::uint8_t* image, std::uint32_t* rvas, int capacity) noexcept {
    __try {
        std::uint8_t* face = image + 0x003c7d50;
        const auto begin = *reinterpret_cast<std::uintptr_t*>(face + 0x34);
        const auto end = *reinterpret_cast<std::uintptr_t*>(face + 0x38);
        if (end < begin || (end - begin) % 4 || end - begin > 64 * 4) return -1;
        int count = 0;
        for (auto at = begin; at < end && count < capacity; at += 4) {
            auto* screen = *reinterpret_cast<std::uint8_t**>(at);
            rvas[count++] = screen ? static_cast<std::uint32_t>(*reinterpret_cast<std::uintptr_t*>(screen) -
                                                               reinterpret_cast<std::uintptr_t>(image)) : 0;
        }
        return count;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

std::uint32_t server_interface();

// spawn (harness only): a unit placed the way the map loader places one.
// CGameMOBFile::Load hands each mob unit record (a CMEUnit) to
// CWorldServer::AddMapObject 005927C0, which creates the object, sets its
// mask, primary/secondary texture and body-part names, SetupStats and
// WearAll from the prototype, its player and enemy mask, and initialises the
// unit; the Dejavu addon calls the same entry mid-game for its traps. The
// script's AddUnitToServer reaches AddMapObject through CreatePersonage and
// DeployPersonages, which leave both textures empty for non-human races and
// dereference an unknown prototype (the stock game crashes on "Goblin00").
// Here the record is built by the game's CMEUnit constructor 0046C010 (its
// defaults: id -1, shadow, "none" names, complection 0.5), its CString fields
// assigned by CString::operator=(LPCSTR) 00700AF9 and the body parts by
// CStringArray::SetAtGrow(int, LPCSTR) 006FEBCE (statically linked MFC),
// and destroyed by ~CMEUnit 0046C140. Layouts (ei-re structs, exact):
// CMEBasicObject p_nID +18, p_ptPosition +20, p_qRotation +2C (w x y z);
// CMEObject p_tidTypeID +50, p_szTemplate +54, p_Complection +60 (strength
// grace tall), p_nPlayer +6E, p_szPrimTexture +70, p_szSecTexture +74,
// p_aBodyParts +78; CMEUnit (size 324h) p_szUnitPrototype +20C.
// The database is theUnitDatabase 007AF358: prototypes (CDatabase, its data
// vector first) at +0, races at +40; SUDBUnitPrototype (120h) szName +0,
// dwRaceIndex +8, dwSkinIndex +C, dwHair +10, fStrength/fGrace/fTall +14..;
// SUDBRaceModel (168h) tidTypeID +4, szMaskName +E8, szPrimaryTextures +EC,
// szSecondaryTextures +F8 (vectors of CString). The game's own lookup
// (CDatabase::operator[] by name) is exact and case-sensitive.
constexpr std::uintptr_t kUnitDatabaseRva = 0x003af358;
constexpr std::uintptr_t kMEUnitCtorRva = 0x0006c010, kMEUnitDtorRva = 0x0006c140;
constexpr std::uintptr_t kAddMapObjectRva = 0x001927c0;
constexpr std::uintptr_t kCStringAssignRva = 0x00300af9, kStringArraySetAtGrowRva = 0x002febce;
constexpr std::size_t kMEUnitSize = 0x324, kPrototypeSize = 0x120, kRaceSize = 0x168;
constexpr int kObjectTypeHuman = 50; // MOTID_HUMAN
using MEUnitCtorFn = void*(__thiscall*)(void*);
using MEUnitDtorFn = void(__thiscall*)(void*);
using AddMapObjectFn = int(__thiscall*)(void*, void*);
using CStringAssignFn = void*(__thiscall*)(void*, const char*);
using StringArraySetAtGrowFn = void(__thiscall*)(void*, int, const char*);

struct SpawnRecord {
    std::string prototype, model, prim, sec;
    std::vector<std::string> parts;
    int type = 0, player = 0;
    float position[3]{}, rotation[4]{1, 0, 0, 0}, complection[3]{0.5f, 0.5f, 0.5f};
};

// The vector [begin, end) of `stride`-byte entries at `vector` (stlport:
// start, finish, end of storage); false when it does not look like one.
bool spawn_vector(const std::uint8_t* vector, std::size_t stride, const std::uint8_t*& begin, std::size_t& count) noexcept {
    __try {
        begin = *reinterpret_cast<const std::uint8_t* const*>(vector);
        const auto* end = *reinterpret_cast<const std::uint8_t* const*>(vector + 4);
        if (!begin || end < begin || std::size_t(end - begin) % stride || std::size_t(end - begin) / stride > 20000) return false;
        count = std::size_t(end - begin) / stride;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A CString's text, or nullptr when unreadable.
const char* spawn_cstring(const std::uint8_t* field) noexcept {
    __try {
        const char* text = *reinterpret_cast<const char* const*>(field);
        return text && std::memchr(text, 0, 256) ? text : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// Fills `record` from the unit database; an error, empty when the prototype
// (and any model override) is in it.
std::string spawn_from_database(std::uint8_t* image, SpawnRecord& record) {
    const std::uint8_t* database = image + kUnitDatabaseRva;
    const std::uint8_t *prototypes = nullptr, *races = nullptr;
    std::size_t prototype_count = 0, race_count = 0;
    if (!spawn_vector(database, kPrototypeSize, prototypes, prototype_count) ||
        !spawn_vector(database + 0x40, kRaceSize, races, race_count))
        return "spawn: unit database unreadable";
    const std::uint8_t* prototype = nullptr;
    for (std::size_t i = 0; i < prototype_count && !prototype; ++i) {
        const char* name = spawn_cstring(prototypes + i * kPrototypeSize);
        if (name && record.prototype == name) prototype = prototypes + i * kPrototypeSize;
    }
    if (!prototype)
        return text_of("spawn: no prototype \"%s\" in the unit database (%u prototypes; names are exact and case-sensitive)",
                       record.prototype.c_str(), static_cast<unsigned>(prototype_count));
    std::uint32_t race_index = 0, skin = 0, hair = 0;
    std::memcpy(&race_index, prototype + 0x8, 4);
    std::memcpy(&skin, prototype + 0xc, 4);
    std::memcpy(&hair, prototype + 0x10, 4);
    std::memcpy(record.complection, prototype + 0x14, sizeof(record.complection));
    if (race_index >= race_count) return text_of("spawn: prototype race index %u outside %u races", race_index, unsigned(race_count));
    const std::uint8_t* race = races + race_index * kRaceSize;
    std::memcpy(&record.type, race + 0x4, 4);
    const char* mask = spawn_cstring(race + 0xe8);
    if (!mask) return "spawn: race mask unreadable";
    if (record.model.empty()) record.model = mask;
    else {
        bool known = false;
        for (std::size_t i = 0; i < race_count && !known; ++i) {
            const char* other = spawn_cstring(races + i * kRaceSize + 0xe8);
            known = other && record.model == other;
        }
        if (!known) return text_of("spawn: model \"%s\" is no race's mask in the unit database", record.model.c_str());
    }
    // Defaults: the race's primary texture at the prototype's skin index (its
    // first when the index is past the list) and its first secondary texture.
    const auto pick = [](const std::uint8_t* vector, std::size_t index) -> std::string {
        const std::uint8_t* begin = nullptr;
        std::size_t count = 0;
        if (!spawn_vector(vector, 4, begin, count) || !count) return {};
        const char* text = spawn_cstring(begin + 4 * (index < count ? index : 0));
        return text ? text : "";
    };
    if (record.prim.empty()) record.prim = pick(race + 0xec, skin);
    if (record.sec.empty()) record.sec = pick(race + 0xf8, 0);
    // Body parts: the human list CWorldServer::DeployPersonages forms (hair
    // hr.NN from the prototype, none for -1); other races carry none.
    if (record.type == kObjectTypeHuman) {
        for (const char* part : {"hd", "bd", "hp", "hr", "lh1", "lh2", "lh3", "rh1", "rh2", "rh3",
                                 "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"}) {
            if (std::strcmp(part, "hr") != 0) record.parts.push_back(part);
            else if (hair != 0xffffffffu) record.parts.push_back(text_of("hr.%.2u", hair));
        }
    }
    return {};
}

// The game calls; SEH so a fault inside them fails the command.
int spawn_call(std::uint8_t* image, const SpawnRecord& record, std::uint8_t* unit, int& id) noexcept {
    __try {
        reinterpret_cast<MEUnitCtorFn>(image + kMEUnitCtorRva)(unit);
        const auto assign = reinterpret_cast<CStringAssignFn>(image + kCStringAssignRva);
        std::memcpy(unit + 0x20, record.position, sizeof(record.position));
        std::memcpy(unit + 0x2c, record.rotation, sizeof(record.rotation));
        std::memcpy(unit + 0x50, &record.type, 4);
        std::memcpy(unit + 0x60, record.complection, sizeof(record.complection));
        unit[0x6e] = static_cast<std::uint8_t>(record.player);
        assign(unit + 0x54, record.model.c_str());
        if (!record.prim.empty()) assign(unit + 0x70, record.prim.c_str());
        if (!record.sec.empty()) assign(unit + 0x74, record.sec.c_str());
        assign(unit + 0x20c, record.prototype.c_str());
        const auto set = reinterpret_cast<StringArraySetAtGrowFn>(image + kStringArraySetAtGrowRva);
        for (std::size_t i = 0; i < record.parts.size(); ++i) set(unit + 0x78, int(i), record.parts[i].c_str());
        id = reinterpret_cast<AddMapObjectFn>(image + kAddMapObjectRva)(image + kServerRva, unit);
        reinterpret_cast<MEUnitDtorFn>(image + kMEUnitDtorRva)(unit);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

std::string camera_off_map(float x, float y);

// True when `entry` starts with a jmp into the module holding this code.
bool jump_into_this_module(const std::uint8_t* entry) noexcept {
    __try {
        if (entry[0] != 0xe9) return false;
        std::int32_t offset = 0;
        std::memcpy(&offset, entry + 1, 4);
        const std::uint8_t* target = entry + 5 + offset;
        HMODULE module = nullptr, self = nullptr;
        constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(target), &module) &&
               GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&jump_into_this_module), &self) && module == self;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string apply_spawn(const HarnessCommand& command, bool& ok) {
    ok = false;
    State& s = state();
    // Stock entry bytes (static: a nested initializer_list would not outlive its pair).
    struct SpawnSite { std::uintptr_t rva; std::uint8_t size; std::uint8_t bytes[11]; };
    static const SpawnSite kSpawnSites[] = {
        {kMEUnitCtorRva, 9, {0x6a,0xff,0x68,0x84,0x53,0x71,0x00,0x64,0xa1}},
        {kMEUnitDtorRva, 9, {0x6a,0xff,0x68,0x48,0x54,0x71,0x00,0x64,0xa1}},
        {kAddMapObjectRva, 10, {0x55,0x8b,0xec,0x6a,0xff,0x68,0x30,0x61,0x72,0x00}},
        {kCStringAssignRva, 10, {0x56,0x57,0x8b,0x7c,0x24,0x0c,0x8b,0xf1,0x85,0xff}},
        {kStringArraySetAtGrowRva, 11, {0x56,0x8b,0x74,0x24,0x08,0x57,0x8b,0xf9,0x3b,0x77,0x08}}};
    for (const auto& site : kSpawnSites)
        // AddMapObject is the renderer's own retained admission hook
        // (engine_bridge.cpp kRetainedAdmissionEntryRva): a jump into this
        // module there is the path every game caller takes.
        if (std::memcmp(s.image + site.rva, site.bytes, site.size) != 0 &&
            !(site.rva == kAddMapObjectRva && jump_into_this_module(s.image + site.rva))) {
            // Name what sits there instead (bytes, and a jump's target module).
            std::string bytes;
            for (std::size_t i = 0; i < 12; ++i) bytes += text_of(i ? " %02x" : "%02x", s.image[site.rva + i]);
            wchar_t module[64]{};
            const std::uint8_t* target = raw_replacement_target(s.image + site.rva, module, 64);
            char name[64]{};
            WideCharToMultiByte(CP_UTF8, 0, module, -1, name, sizeof(name), nullptr, nullptr);
            return text_of("spawn: game bytes differ at 0x%08lX: %s%s%s", static_cast<unsigned long>(0x00400000 + site.rva),
                           bytes.c_str(), target ? "; jump into " : "", target ? name : "");
        }
    SpawnRecord record;
    record.prototype = command.name;
    const auto first = command.value.find('\n'), second = command.value.find('\n', first + 1);
    record.model = command.value.substr(0, first);
    record.prim = command.value.substr(first + 1, second - first - 1);
    record.sec = command.value.substr(second + 1);
    if (auto error = spawn_from_database(s.image, record); !error.empty()) return error;
    const std::string off_map = camera_off_map(command.number[0], command.number[1]);
    if (!off_map.empty()) return "spawn: " + off_map;
    record.position[0] = command.number[0];
    record.position[1] = command.number[1];
    const float half = command.number[2] * kPi / 360.0f;
    record.rotation[0] = std::cos(half);
    record.rotation[3] = std::sin(half);
    record.player = static_cast<int>(command.frames);
    alignas(8) std::uint8_t unit[kMEUnitSize + 16]{};
    int id = -1;
    if (spawn_call(s.image, record, unit, id) != 0) return "spawn: the game faulted placing the unit";
    if (id < 0) return text_of("spawn: AddMapObject refused the unit (returned %d)", id);
    ok = true;
    return text_of("spawned id=%d prototype=\"%s\" model=%s prim=%s sec=%s parts=%u at %.2f %.2f yaw=%g player=%d",
                   id, record.prototype.c_str(), record.model.c_str(), record.prim.empty() ? "none" : record.prim.c_str(),
                   record.sec.empty() ? "none" : record.sec.c_str(), static_cast<unsigned>(record.parts.size()),
                   record.position[0], record.position[1], command.number[2], record.player);
}

std::string screen_stack_text() {
    static const struct { std::uint32_t rva; const char* name; } kScreens[] = {
        {0x003405ac, "base"}, {0x00340688, "main_menu"}, {0x00340764, "mission"}, {0x00340840, "debug"},
        {0x0034091c, "camp"}, {0x00340b3c, "global_map"}, {0x00340c3c, "end_game"}, {0x00340d18, "exchange"},
        {0x00340e28, "tutorial"}, {0x003411bc, "objectives"}, {0x003412a4, "escape_menu"},
        {0x00341388, "save_load"}, {0x00341cd0, "connection"}, {0x00341fac, "options"},
        {0x0034214c, "choose_pers"}, {0x00342348, "message_box"}, {0x0033f7f8, "autoplay"}};
    if (lia_harness()) return "screens=not read on LiA";
    std::uint32_t rvas[16]{};
    const int count = screen_stack_rvas(state().image, rvas, 16);
    if (count < 0) return "screens=unreadable";
    std::string text = "screens=";
    for (int i = 0; i < count; ++i) {
        const char* name = nullptr;
        for (const auto& screen : kScreens) if (screen.rva == rvas[i]) name = screen.name;
        if (i) text += '>';
        text += name ? std::string(name) : text_of("%08x", rvas[i]);
    }
    return text + text_of(" server_interface=%u", server_interface());
}

// Empty when (x, y) is a stop the game's own camera can reach; "-" (NaN)
// keeps the current coordinate and is not checked.
std::string camera_off_map(float x, float y) {
    std::uint8_t* map = field<std::uint8_t*>(state().image, kCurrentMapRva);
    if (!map) return "camera: no current map";
    const float width = field<float>(map, kMapWidth), height = field<float>(map, kMapHeight);
    const bool outside = (!std::isnan(x) && !(x >= 0.0f && x <= width - 1.0f)) ||
                         (!std::isnan(y) && !(y >= 0.0f && y <= height - 1.0f));
    return outside ? text_of("camera %.3f %.3f outside the map %.0fx%.0f", x, y, width, height) : std::string();
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

// camera_unit, one step a world frame: the first applies the requested yaw
// and pitch with the pivot at the unit and the game camera distance at the
// requested one; each later one, once a world view captured after the last
// pose is in, reads where the unit projects and how far it is from the eye.
// Moving the pivot moves the whole camera with it, so the pivot moves by the
// offset from the unit to the point at the unit's height that projects to the
// screen centre (clip x = clip y = 0 of that view). The eye is where clip
// x, y and w are all 0 (independent of the projection's scale); the game
// camera distance is scaled by requested / eye-to-unit, because the game's
// pivot sits low on the screen (a centred unit lies well beyond it: save1491
// wolf 1077 at distance 6 ended 24 m from the pivot, behind a hill). The
// pivot's height follows the terrain (SetAnchorPoint), so a few corrections
// settle both.
void camera_unit_step(const HarnessCommand& command) {
    State& s = state();
    s.world_ok = true;
    if (s.views_noted < s.unit_applied_view + (s.unit_step ? 2u : 1u)) {
        s.world_detail = text_of("camera_unit: waiting for a world view (correction %u)", s.unit_step);
        return;
    }
    const auto finish = [&](bool ok, const std::string& detail) {
        s.world_ok = ok; s.world_detail = detail; s.unit_finished = true;
    };
    // @x,y: the first view picks the nearest unit within 6 m (horizontal);
    // the corrections then follow that unit.
    if (command.name == "@" && s.unit_step == 0) {
        constexpr float kReach = 6.0f;
        const HarnessUnitPosition* nearest = nullptr;
        float best = kReach * kReach;
        for (const auto& candidate : s.view_units) {
            const float dx = candidate.position[0] - command.number[3], dy = candidate.position[1] - command.number[4];
            if (dx * dx + dy * dy <= best) { best = dx * dx + dy * dy; nearest = &candidate; }
        }
        if (!nearest) {
            finish(false, text_of("camera_unit: no unit within %.0f m of %.2f,%.2f among the world's %u units "
                                  "(drawall on publishes units the party does not see)",
                                  kReach, command.number[3], command.number[4], static_cast<unsigned>(s.view_units.size())));
            return;
        }
        s.unit_target_id = nearest->id;
    }
    const std::int32_t id = command.name == "@" ? s.unit_target_id : static_cast<std::int32_t>(command.frames);
    const HarnessUnitPosition* unit = nullptr;
    for (const auto& candidate : s.view_units)
        if (candidate.id == id) { unit = &candidate; break; }
    if (!unit) {
        finish(false, text_of("camera_unit: unit %d is not among the world's %u units (dump_units lists them; "
                              "drawall on publishes units the party does not see)",
                              id, static_cast<unsigned>(s.view_units.size())));
        return;
    }
    const float* p = unit->position;
    const auto& m = s.view_clip;
    if (s.unit_step == 0) {
        s.unit_anchor[0] = p[0];
        s.unit_anchor[1] = p[1];
        s.unit_distance = command.number[2];
    } else {
        const float x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
        const float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
        const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
        const float nx = w > 1e-5f ? x / w : 99.0f, ny = w > 1e-5f ? y / w : 99.0f;
        const std::string where = (command.name == "@" ? text_of("nearest to %.2f,%.2f: ", command.number[3], command.number[4]) :
                                   std::string()) +
                                  text_of("unit %d at %.2f %.2f %.2f, NDC %.3f %.3f after %u correction(s)",
                                          id, p[0], p[1], p[2], nx, ny, s.unit_step - 1);
        // The eye: rows 0, 1 and 3 of world-to-clip are 0 there (Cramer).
        const float r[3][4] = {{m[0], m[1], m[2], m[3]}, {m[4], m[5], m[6], m[7]}, {m[12], m[13], m[14], m[15]}};
        const auto det3 = [](float a0, float a1, float a2, float b0, float b1, float b2, float c0, float c1, float c2) {
            return a0 * (b1 * c2 - b2 * c1) - a1 * (b0 * c2 - b2 * c0) + a2 * (b0 * c1 - b1 * c0);
        };
        const float dm = det3(r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
        float eye_to_unit = -1.0f;
        if (std::abs(dm) > 1e-12f) {
            const float ex = det3(-r[0][3], r[0][1], r[0][2], -r[1][3], r[1][1], r[1][2], -r[2][3], r[2][1], r[2][2]) / dm;
            const float ey = det3(r[0][0], -r[0][3], r[0][2], r[1][0], -r[1][3], r[1][2], r[2][0], -r[2][3], r[2][2]) / dm;
            const float ez = det3(r[0][0], r[0][1], -r[0][3], r[1][0], r[1][1], -r[1][3], r[2][0], r[2][1], -r[2][3]) / dm;
            eye_to_unit = std::sqrt((p[0] - ex) * (p[0] - ex) + (p[1] - ey) * (p[1] - ey) + (p[2] - ez) * (p[2] - ez));
        }
        std::uint8_t* camera = s.image + kCameraRva;
        const float wanted = command.number[2];
        const std::string distance = text_of("eye to unit %.2f m (requested %.2f); game camera distance %.3f",
                                             eye_to_unit, wanted, field<float>(camera, kCameraDistance));
        // Within 0.03 NDC and 5% of the distance is done. A unit that keeps
        // moving (its idle animation moves the root even while paused:
        // save1491 unit 37 stayed 0.01-0.02 off after 8 corrections) ends
        // near the centre (within 0.1 NDC and 15%, said so) after 16;
        // farther off it fails.
        const float off = (std::max)(std::abs(nx), std::abs(ny));
        const float range = eye_to_unit > 0.0f ? std::abs(eye_to_unit - wanted) / wanted : 1.0f;
        const bool settled = off < 0.03f && range < 0.05f;
        if (settled || (s.unit_step > 16 && off < 0.1f && range < 0.15f)) {
            finish(true, (settled ? "" : "near the centre (the unit moves between frames): ") + where + "; " +
                         distance + "; " + camera_state());
            return;
        }
        if (s.unit_step > 16) {
            // The pivot rests on the terrain: from higher ground the centre
            // ray needs room to come down to the unit (save1491 wolf 1077,
            // 9 m below the hill east of it, stayed 29 m away at distance 0.5).
            const float above = field<float>(camera, kCameraAnchor + 8) - p[2];
            finish(false, "camera_unit: not centred: " + where + "; " + distance + "; " + camera_state() +
                          (eye_to_unit > wanted && above > 2.0f ?
                           text_of("; the pivot rests on terrain %.1f m above the unit: use a steeper pitch or another yaw", above) : ""));
            return;
        }
        if (eye_to_unit > 0.0f)
            s.unit_distance = std::clamp(s.unit_distance * std::clamp(wanted / eye_to_unit, 0.25f, 4.0f), 0.5f, 500.0f);
        const float a = m[0], b = m[1], c = m[4], d = m[5];
        const float e = -(m[2] * p[2] + m[3]), f = -(m[6] * p[2] + m[7]);
        const float det = a * d - b * c;
        const float cx = std::abs(det) > 1e-12f ? (e * d - b * f) / det : 0.0f;
        const float cy = std::abs(det) > 1e-12f ? (a * f - e * c) / det : 0.0f;
        if (std::abs(det) <= 1e-12f || m[12] * cx + m[13] * cy + m[14] * p[2] + m[15] <= 0.0f) {
            finish(false, "camera_unit: the screen centre does not reach the unit's height at this pitch: " + where);
            return;
        }
        s.unit_anchor[0] += p[0] - cx;
        s.unit_anchor[1] += p[1] - cy;
    }
    const std::string off_map = camera_off_map(s.unit_anchor[0], s.unit_anchor[1]);
    if (!off_map.empty()) {
        finish(false, "camera_unit: the pivot that centres unit " + std::to_string(id) + " is off the map: " + off_map);
        return;
    }
    HarnessCommand pose;
    pose.verb = HarnessVerb::camera;
    pose.number[0] = s.unit_anchor[0];
    pose.number[1] = s.unit_anchor[1];
    pose.number[2] = command.number[0];
    pose.number[3] = command.number[1];
    pose.number[4] = s.unit_distance;
    apply_camera(pose);
    s.unit_applied_view = s.views_noted;
    ++s.unit_step;
    s.world_detail = text_of("camera_unit: correction %u applied", s.unit_step);
}

// BE0FC330...94E2: the portrait double-click at 00638FBA calls the
// pointer overload of SetCarrier. It loads object+0C and does the same work
// as our existing ID overload 0040D420. Camera Step (0040FBBA) resolves
// that ID through the current client manager each frame; it owns smoothing.
// The party leader is the first party record, not the first visible portrait.
// 00636206..34 calls 00578BA0, which returns [[[client+20C]+20]+1C]+70
// (00578BA0..B0); the site then reads count at +34 and records at +30
// (party +A4/+A0 of that object) and the first record's ID at +1C.
// 00602D2D uses that ID to centre the base-map camera. No object pointer
// survives this call.
std::string hero_contract_error() {
    const State& s = state();
    if (!s.game_verified || lia_harness()) return "camera hero: not verified on this game image (LiA unported)";
    auto error = entry_error(s, 0x001699f0,
        {0x8b,0x81,0x0c,0x02,0x00,0x00,0x85,0xc0,0x74,0x04,0x8b,0x40,0x20,0xc3,0x33,0xc0,0xc3});
    if (error.empty()) error = entry_error(s, 0x0000fbba,
        {0x8b,0x86,0x1c,0x01,0x00,0x00,0x8b,0x0d,0xf8,0xf5,0x7a,0x00,0x50,0xe8,0x74,0x43,0xff,0xff});
    if (error.empty()) error = entry_error(s, 0x00003f40,
        {0x53,0x8b,0x59,0x24,0x56,0x8b,0x71,0x20,0x2b,0xde,0x8b,0x74,0x24,0x0c,0xc1,0xfb,0x02,0x8b,0xc6,0x33});
    if (error.empty()) error = entry_error(s, 0x00178ba0,
        {0xb9,0xb8,0xf5,0x7a,0x00,0xe8,0x46,0x0e,0xff,0xff,0x8b,0x40,0x1c,0x83,0xc0,0x70,0xc3});
    if (error.empty()) error = entry_error(s, 0x00236206,
        {0xb9,0xa5,0xfe,0x7a,0x00,0xe8,0x90,0x29,0xf4,0xff,0x8b,0x50,0x34,0x83,0xc0,0x2c,0x33,0xf6,0x33,0xdb,
         0x85,0xd2,0x89,0x44,0x24,0x1c,0xc6,0x44,0x24,0x13,0x00,0x89,0x5c,0x24,0x14,0x7e,0x45,0x8b,0x4c,0x24,
         0x40,0x8b,0x40,0x04,0x83,0xc0,0x1c});
    if (error.empty()) error = entry_error(s, kSetCarrierRva + 7, {0x89,0x81,0x1c,0x01,0x00,0x00});
    return error;
}

const char* follow_party_leader(std::int32_t& id) noexcept {
    auto* image = state().image;
    __try {
        auto* world = image + kWorldClientRva;
        // The chain 00578BA0 walks (its callee 005699F0 returns 0 without a player).
        auto* player = field<std::uint8_t*>(world, kWorldPlayer);
        auto* specific = player ? field<std::uint8_t*>(player, kPlayerSpecific) : nullptr;
        auto* party = specific ? field<std::uint8_t*>(specific, 0x1c) : nullptr;
        if (!party) return "camera hero: no local player";
        const auto count = field<std::int32_t>(party, 0xa4);
        auto* members = field<std::uint8_t*>(party, 0xa0);
        if (count <= 0 || !members) return "camera hero: party has no leader";
        id = field<std::int32_t>(members, 0x1c);
        auto* manager = field<void*>(world, 0x40);
        using Find = void*(__thiscall*)(void*, std::int32_t);
        if (!manager || !reinterpret_cast<Find>(image + 0x00003f40)(manager, id))
            return "camera hero: leader absent from client world";
        // 0040D427 stores this carrier field. Avoid clearing native scroll
        // interpolation again while the camera is already attached.
        if (field<std::int32_t>(image + kCameraRva, 0x11c) != id)
            reinterpret_cast<SetCarrierFn>(image + kSetCarrierRva)(image + kCameraRva, static_cast<std::uint32_t>(id));
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "camera hero: party or camera read/call faulted"; }
}

// True when [at, at+bytes) lies in addon.dll (one module, both ends).
bool addon_span(const std::uint8_t* at, std::size_t bytes) noexcept {
    return at && addon_module_contains(at) && addon_module_contains(at + bytes - 1);
}

// The addon's incoming protection, wherever its build places it: the unit's
// owner (unit+24C, the field party_protection validates) is tested, then
// `cmp byte [owner+108], 2; jae` skips the hit. Searched in the callback and
// in the first function it calls; both must lie in addon.dll.
const char* addon_attack_guard(const std::uint8_t* callback) noexcept {
    constexpr std::uint8_t guard[] = {0x8b,0x80,0x4c,0x02,0x00,0x00,0x85,0xc0,0x74,0x00,
                                      0x80,0xb8,0x08,0x01,0x00,0x00,0x02,0x0f,0x83};
    constexpr std::size_t kWindow = 0x2000, kStub = 0x40;
    const auto has_guard = [&](const std::uint8_t* body) {
        for (std::size_t at = 0; at + sizeof(guard) <= kWindow; ++at) {
            std::size_t i = 0;
            while (i < sizeof(guard) && (i == 9 || body[at + i] == guard[i])) ++i;
            if (i == sizeof(guard)) return true;
        }
        return false;
    };
    if (!addon_span(callback, kWindow)) return "attack callback outside addon.dll";
    __try {
        if (has_guard(callback)) return nullptr;
        for (std::size_t at = 0; at + 5 <= kStub; ++at) {
            if (callback[at] != 0xe8) continue;
            std::int32_t relative;
            std::memcpy(&relative, callback + at + 1, sizeof(relative));
            const std::uint8_t* callee = callback + at + 5 + relative;
            if (!addon_span(callee, kWindow)) continue;
            if (has_guard(callee)) return nullptr;
            break;  // only the callback's first call is the attack body
        }
        return "addon attack callback has no owner+108 >= 2 protection check";
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "addon attack callback unreadable"; }
}

// A raw detour by another module (Dejavu plugin.dll) on top of the addon's
// attack chain: accept it only when its body calls through a pointer that
// holds the addon's own dispatcher for this entry, so the call still runs the
// guarded addon code. What the module adds around that call is not verified.
const std::uint8_t* layered_attack_callback(const std::uint8_t* entry, const std::uint8_t* stock,
                                            std::size_t stock_bytes, std::string& owner) noexcept {
    wchar_t module[64]{};
    const std::uint8_t* detour = raw_replacement_target(entry, module, 64);
    if (!detour || addon_module_contains(detour)) return nullptr;
    char name[64]{};
    for (std::size_t i = 0; i + 1 < sizeof(name) && module[i]; ++i) name[i] = char(module[i] & 0x7f);
    owner = name;
    constexpr std::size_t kBody = 0x1000;
    std::uint8_t body[kBody];
    __try { std::memcpy(body, detour, kBody); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    for (std::size_t at = 0; at + 6 <= kBody; ++at) {
        if (body[at] != 0xff || body[at + 1] != 0x15) continue;
        std::uint32_t slot;
        std::memcpy(&slot, body + at + 2, sizeof(slot));
        const std::uint8_t* callback = nullptr;
        if (layered_framework_dispatcher(entry, reinterpret_cast<const std::uint8_t* const*>(std::uintptr_t(slot)),
                                         stock, stock_bytes, &addon_module_contains, &callback))
            return callback;
    }
    return nullptr;
}

// The addon replaces stock deadly outgoing attacks with an immediate return.
const char* addon_outgoing_return(const std::uint8_t* callback) noexcept {
    if (!addon_span(callback, 3)) return "outgoing callback outside addon.dll";
    __try {
        if (callback[0] != 0xc2 || callback[1] % 4 || callback[2])
            return "addon outgoing callback is not an immediate return";
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "addon outgoing callback unreadable"; }
}

// The verified combat replacement uses >=2 for incoming protection, whereas
// stock Base/LiA use !=0 and reserve >=2 for deadly outgoing attacks. Check
// the installed chain, not merely the presence of addon.dll or an ini value.
std::string addon_god_guard(const std::uint8_t* attack_entry, const std::uint8_t* attack_stock,
                            std::size_t attack_stock_bytes, const EntryResolution& attack,
                            const EntryResolution& outgoing) {
    const std::uint8_t* callback = attack.owner == EntryOwner::framework_chain ? attack.callback : nullptr;
    std::string layered;
    if (!callback && attack.owner == EntryOwner::rejected) {
        callback = layered_attack_callback(attack_entry, attack_stock, attack_stock_bytes, layered);
        if (!callback)
            return text_of("godmode: attack entry is not the addon chain (%s%s%s)",
                           attack.reason ? attack.reason : "unknown", layered.empty() ? "" : ", detour by ",
                           layered.c_str());
    }
    if (!callback) return "godmode: attack entry chain has no addon callback";
    if (const char* error = addon_attack_guard(callback)) return text_of("godmode: %s", error);
    if (outgoing.owner != EntryOwner::framework_chain || !outgoing.callback)
        return text_of("godmode: outgoing deadly-attack entry is not the addon chain (%s)",
                       outgoing.reason ? outgoing.reason : "no callback");
    if (const char* error = addon_outgoing_return(outgoing.callback)) return text_of("godmode: %s", error);
    static bool logged = false;
    if (!layered.empty() && !logged) {
        logged = true;
        bridge_report_event("harness_godmode_layered", text_of(
            "attack entry detour by %s continues into the addon chain; its own code around that call "
            "is not verified", layered.c_str()).c_str());
    }
    return {};
}

std::string party_contract_error() {
    const State& s = state();
    if (!s.god_error.empty()) return s.god_error;
    const bool lia = lia_harness();
    auto error = entry_error(s, lia ? 0x000ceb80 : 0x001699f0,
        {0x8b,0x81,0x0c,0x02,0x00,0x00,0x85,0xc0,0x74,0x04,0x8b,0x40,0x20,0xc3,0x33,0xc0,0xc3});
    if (error.empty()) error = entry_error(s, lia ? 0x000e6c80 : 0x001941c0, {0x39,0x70,0x24,0x74,0x2c});
    if (error.empty()) error = entry_error(s, lia ? 0x000e87e7 : 0x00191757, {0x8b,0x86,0xa4,0x00,0x00,0x00});
    if (error.empty()) error = entry_error(s, lia ? 0x000e8800 : 0x00191768,
        {0x8b,0x8e,0xa0,0x00,0x00,0x00,0x8b,0x54,0x39,0x1c});
    if (error.empty()) error = entry_error(s, lia ? 0x000e882f : 0x00191797, {0x81,0xc7,0x84,0x07,0x00,0x00});
    if (error.empty()) error = entry_error(s, lia ? 0x000cd680 : 0x0015fb00,
        {0x8b,0x54,0x24,0x04,0x8b,0x01,0x56,0x52,0xff,0x90,0xa0,0x00,0x00,0x00,0x8b,0xf0,0x85,0xf6,0x75,0x04,
         0x5e,0xc2,0x04,0x00,0x8b,0x06,0x8b,0xce,0xff,0x50,0x30,0x83,0xe8,0x50,0xf7,0xd8,0x1b,0xc0,0xf7,0xd0,
         0x23,0xc6,0x5e,0xc2,0x04,0x00});
    return error;
}

std::string protection_level(std::uint8_t& level) {
    const State& s = state();
    level = 1;
    if (lia_harness()) {
        auto error = entry_error(s, 0x000c90d0, {0x83,0xec,0x2c,0x53,0x55,0x56,0x57,0x8b,0xf1,0xe8,0x22,0x91,0xf6,0xff});
        if (error.empty()) error = entry_error(s, 0x000c92c1,
            {0x8b,0x82,0x4c,0x02,0x00,0x00,0x85,0xc0,0x74,0x28,0x8a,0x88,0x08,0x01,0x00,0x00,0x84,0xc9,0x0f,0x85,0xff,0x01,0x00,0x00});
        return error;
    }
    constexpr std::uint8_t stock[] = {0x55,0x8b,0xec,0x83,0xec,0x2c,0x53,0x56,0x57,0x8b,0xf1,0xe8,0xf0,0x19,0xf0,0xff};
    const auto attack = resolve_entry_owner(s.image + 0x00153f40, stock, sizeof(stock), 5, &addon_module_contains);
    if (attack.owner == EntryOwner::stock)
        return entry_error(s, 0x00154139,
            {0x8b,0x46,0x1c,0x8b,0x80,0x4c,0x02,0x00,0x00,0x85,0xc0,0x74,0x27,0x8a,0x88,0x08,0x01,0x00,0x00,0x84,0xc9,0x0f,0x85,0x2a,0x02,0x00,0x00});
    constexpr std::uint8_t deadly[] = {0x55,0x8b,0xec,0x83,0xec,0x0c,0x53,0x56,0x8b,0xf1,0x57,0x89,0x75,0xfc,0x8b,0x46};
    const auto outgoing = resolve_entry_owner(s.image + 0x0014f630, deadly, sizeof(deadly), 5, &addon_module_contains);
    auto error = addon_god_guard(s.image + 0x00153f40, stock, sizeof(stock), attack, outgoing);
    if (error.empty()) level = 2;
    return error;
}

struct PartyProtection { int player = -1, units = 0, covered = 0, failed_id = 0; unsigned flag = 0; };

// Base BE0FC330 / LiA C7B085C1: player+A0/A4 is the authoritative party;
// each 0x784-byte member names its unit at +1C. GetUnit checks the type and
// returns null for a missing ID. The incoming attack guard reads unit+24C
// (owner), then owner+108. Validate the entire roster BEFORE a single write.
const char* party_protection(int requested, std::uint8_t level, PartyProtection& out) noexcept {
    State& s = state();
    const bool lia = lia_harness();
    __try {
        if (field<std::uint32_t>(s.image + connector_rva(), 0)) return "world transition pending";
        auto* client = field<std::uint8_t*>(s.image + (lia ? kLiaWorldClientRva : kWorldClientRva), kWorldPlayer);
        if (!client) return "no local client player";
        out.player = field<int>(client, 0x24);
        auto* server = s.image + (lia ? kLiaServerRva : kServerRva);
        auto* player = reinterpret_cast<GetServerPlayerFn>(s.image + (lia ? kLiaGetServerPlayerRva : kGetServerPlayerRva))(server, out.player);
        // GetPlayer returns the server player whose +24 equals this number
        // (cmp [eax+24],esi at 005941C0, checked by party_contract_error).
        if (!player) return "no server player for the local client player number";
        auto* members = field<std::uint8_t*>(player, 0xa0);
        out.units = field<int>(player, 0xa4);
        // Release the owner flag even when a dead/missing member means the
        // current roster cannot satisfy an on/expect assertion.
        if (requested == 0) {
            field<std::uint8_t>(player, kPlayerGod) = 0;
            out.flag = field<std::uint8_t>(player, kPlayerGod);
            if (out.flag) return "could not clear player protection";
        }
        if (!members || out.units <= 0 || out.units > 65536) return "party roster is empty or unreadable";
        using GetUnit = std::uint8_t*(__thiscall*)(void*, int);
        const auto get_unit = reinterpret_cast<GetUnit>(s.image + (lia ? 0x000cd680 : 0x0015fb00));
        for (int i = 0; i < out.units; ++i) {
            out.failed_id = field<int>(members, std::size_t(i) * 0x784 + 0x1c);
            auto* unit = get_unit(server, out.failed_id);
            if (requested == 0) {
                if (unit && field<std::uint8_t*>(unit, 0x24c) == player) ++out.covered;
                continue;
            }
            if (!unit) return "party member absent from server";
            if (field<std::uint8_t*>(unit, 0x24c) != player) return "party member has a different or cleared owner";
        }
        if (requested == 0) { out.failed_id = 0; return nullptr; }
        if (requested >= 0) field<std::uint8_t>(player, kPlayerGod) = requested ? level : 0;
        for (int i = 0; i < out.units; ++i) {
            out.failed_id = field<int>(members, std::size_t(i) * 0x784 + 0x1c);
            auto* unit = get_unit(server, out.failed_id);
            if (!unit) return "party member disappeared";
            auto* owner = field<std::uint8_t*>(unit, 0x24c);
            if (owner != player) return "party member owner changed";
            out.flag = field<std::uint8_t>(owner, kPlayerGod);
            if (requested == 0 ? out.flag != 0 : out.flag < level) return "party member protection flag does not satisfy the active combat path";
            ++out.covered;
        }
        out.failed_id = 0;
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "party protection read/write faulted"; }
}

bool apply_party_protection(const HarnessCommand& command, std::string& detail) {
    detail = party_contract_error();
    std::uint8_t level = 1;
    if (detail.empty()) detail = protection_level(level);
    if (!detail.empty()) return false;
    // Any loaded zone of the local single-player server (missions, and hub
    // zones whose zone type is not 1); never the menu or a remote server.
    const LocalServerState server = bridge_local_server_state();
    if (!server.readable || server.mode != 1 || !server.loaded) {
        detail = text_of("godmode: no local single-player world (server %s mode=%u loaded=%u zone_type=%u)",
                         server.readable ? "readable" : "unreadable", server.mode, server.loaded, server.zone_type);
        return false;
    }
    PartyProtection result;
    const char* error = party_protection(command.verb == HarnessVerb::expect_godmode ? -1 : int(command.frames), level, result);
    detail = text_of("godmode %s: %s%d/%d units; player=%d flag=%u required=%u%s%s (unit=%d)",
        command.verb == HarnessVerb::expect_godmode || command.frames ? "on" : "off",
        command.verb == HarnessVerb::godmode && !command.frames ? "cleared=" : "protected=",
        result.covered, result.units, result.player, result.flag, unsigned(level), error ? "; " : "", error ? error : "", result.failed_id);
    return error == nullptr;
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
    s.hero_follow = false;
    begin_event_window();
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
    if (s.world_ok) s.event_epoch = epoch;
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

bool harness_wants_world_view() noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    const State& s = state();
    return s.enabled && !s.queue.empty() && s.queue.front().command.verb == HarnessVerb::camera_unit;
}

void harness_note_world_view(std::vector<HarnessUnitPosition> units,
                             const std::array<float, 16>& world_to_clip) noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    State& s = state();
    s.view_units = std::move(units);
    s.view_clip = world_to_clip;
    ++s.views_noted;
}

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
            retire_front(true, camera_state() + " " + screen_stack_text());
            continue;
        case HarnessVerb::note:
            set_banner(command.name);
            retire_front(true, "banner set");
            continue;
        case HarnessVerb::expect_event_absent:
        case HarnessVerb::expect_event_present: {
            // x64 host renderer events go to its host.log, which this reader
            // does not see: refuse instead of passing event_absent.
            if (x64remote::core_active()) {
                retire_front(false, "expect event unavailable on x64 host: renderer events are not in bridge.jsonl"); continue;
            }
            if (!s.event_epoch || s.event_epoch != bridge_current_world_epoch()) {
                retire_front(false, "expect event: no completed harness load_save boundary for this world"); continue;
            }
            if (entry.progress == 0) {
                entry.started_ms = GetTickCount64();
                entry.command.value = s.event_load_marker + ":" + std::to_string(entry.id);
                entry.progress = 1;
            }
            if (entry.progress == 1) {
                // The log reader below owns its own timeout and reason.
                if (harness_timed_out(entry.started_ms, GetTickCount64(), 5)) {
                    retire_front(false, "expect event: renderer did not acknowledge within 5 seconds"); continue;
                }
                return &command; // the renderer acknowledges at its present
            }
            bool ok = false; std::string detail;
            if (!finish_event_expectation(entry, ok, detail)) return nullptr;
            retire_front(ok, detail);
            continue;
        }
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
                retire_front(false, "no world frame within 600 presents; " + screen_stack_text());
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
                                        "the game-over box or a dialog stops world frames); " + screen_stack_text());
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
        case HarnessVerb::camera_unit:
            if (!s.game_verified) { retire_front(false, "unsupported game image"); continue; }
            if (on_travel_map()) { s.world_pending = false; retire_front(false, kTravelMapDetail); continue; }
            if (!entry.posted) {
                entry.posted = true;
                s.view_units.clear();
                s.unit_applied_view = s.views_noted;
                s.unit_step = 0;
                s.unit_finished = false;
            } else if (s.world_done && (s.unit_finished || !s.world_ok)) {
                s.world_done = false;
                retire_front(s.world_ok, s.world_detail);
                continue;
            }
            if (++entry.progress > kWorldCommandTimeoutPresents) {
                s.world_pending = s.world_done = false;
                retire_front(false, "camera_unit: not settled within 600 presents (" + s.world_detail + ")");
                continue;
            }
            s.world_pending = true;
            s.world_done = false;
            return nullptr;
        case HarnessVerb::equip_cycle:
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
            // equip_cycle runs only at the world frame; the renderer times camera_path.
            return command.verb == HarnessVerb::camera_path ? &command : nullptr;
        case HarnessVerb::open_outfitting: {
            if (!s.game_verified) { retire_front(false, "unsupported game image"); continue; }
            if (!entry.posted) {
                entry.posted = true;
                s.world_pending = true;
                s.world_done = false;
                s.camp_detail.clear();
                s.camp_frames_at_open = s.camp_frames;
                s.outfit_started_ms = GetTickCount64();
            }
            if (s.world_done) {
                if (!s.world_ok) { s.world_done = false; retire_front(false, s.world_detail); continue; }
                if (s.camp_frames > s.camp_frames_at_open + 2) {
                    s.world_done = false;
                    retire_front(true, "outfitting screen open: " + s.camp_detail + "; " + s.world_detail);
                    continue;
                }
            } else if (on_travel_map()) {
                s.world_pending = false; retire_front(false, kTravelMapDetail); continue;
            }
            if (harness_timed_out(s.outfit_started_ms, GetTickCount64(), 30)) {
                const bool pressed = s.world_done && s.world_ok;
                s.world_pending = false; s.world_done = false;
                retire_front(false, pressed ? "no camp screen within 30 s after the go-camp button (the addon "
                    "refuses in combat, with enemies near or in an excluded zone): " + s.world_detail
                    : std::string("no world frame within 30 s"));
                continue;
            }
            return nullptr;
        }
        case HarnessVerb::outfit_equip:
        case HarnessVerb::scroll_items:
            if (!s.game_verified) { retire_front(false, "unsupported game image"); continue; }
            if (entry.posted && s.camp_done && entry.progress >= command.frames) {
                s.camp_pending = s.camp_done = false;
                retire_front(s.camp_ok, s.camp_detail);
                continue;
            }
            if (!entry.posted) { entry.posted = true; s.camp_pending = true; s.camp_done = false; s.camp_idle_presents = 0; }
            if (++s.camp_idle_presents > kWorldCommandTimeoutPresents) {
                s.camp_pending = s.camp_done = false;
                retire_front(false, "no outfitting-screen frame within 600 presents (open_outfitting first)");
                continue;
            }
            return nullptr;
        case HarnessVerb::camera:
        case HarnessVerb::drawall:
        case HarnessVerb::spawn:
        case HarnessVerb::camera_hero:
        case HarnessVerb::clock_pin:
        case HarnessVerb::clock_release:
            if (!s.game_verified) {
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
                retire_front(false, "no world frame within 600 presents; " + screen_stack_text());
                continue;
            }
            return nullptr;
        case HarnessVerb::godmode:
        case HarnessVerb::expect_godmode: {
            // Present consumes the command on the game thread. Applying now
            // avoids letting another simulation tick run before the next Draw.
            std::string detail;
            const bool ok = apply_party_protection(command, detail);
            retire_front(ok, detail);
            continue;
        }
        case HarnessVerb::expect_hero_in_view:
            retire_front(false, "hero_in_view unavailable: no per-leader rendered coverage; world depth may already be cleared at Present");
            continue;
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
// A pause this function set lasts exactly as long as the hold: it is released
// when the hold ends (reveal, or the hold's own caps), when the renderer
// stops reporting the hold, and when the hold stops counting world frames, so
// it can never leave the game paused. No wall clock cap: on a slow GPU the
// capped hold lasts longer than any fixed time (#305).
void load_pause_world_frame(void* world_client, bool hold, std::uint64_t epoch,
                            bool single_player_mission, bool reporting,
                            bool previous_draw_counted) noexcept {
    static int usable = -1;
    static bool ours = false;
    static std::uint64_t decided_epoch = 0;
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
    static ULONGLONG paused_at = 0;  // for the release line only
    // The hold counts held time and its caps only on world Draws that
    // complete (the completion counter). Draws that stop completing while our
    // pause is on would hold the picture and the pause forever (#305, x86 run
    // of 5abac2d6): after this many such Draws in a row the pause lets go.
    constexpr unsigned kStalledDrawLimit = 30;
    static unsigned stalled_draws = 0;
    stalled_draws = ours && !previous_draw_counted ? stalled_draws + 1 : 0;
    const bool stalled = stalled_draws >= kStalledDrawLimit;
    const ULONGLONG now = GetTickCount64();
    if (!reporting || stalled) hold = false;
    if (hold && single_player_mission && epoch && epoch != decided_epoch) {
        if (ours) {               // a new load while still holding: keep our pause
            decided_epoch = epoch;
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
        // Released only once the game's flags read unpaused. Without a player
        // (the mission was left for the menu, a failed load) the setter cannot
        // run: ours stays and is retried at every world Draw, and the next
        // mission's hold keeps and then releases it.
        const bool was_paused = paused();
        if (was_paused && !set(false)) return;
        ours = false;
        char detail[128];
        std::snprintf(detail, sizeof(detail), "%s after %llu ms%s", was_paused ? "resumed" : "already unpaused",
                      static_cast<unsigned long long>(now - paused_at),
                      !reporting ? ": the renderer stopped reporting the hold" :
                      stalled ? ": the hold counted no world frame for 30 world Draws" : " at reveal");
        bridge_report_event("load_pause", detail);
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

// Outfitting screen (2026-11-06). Read from game.exe BE0FC330 and the
// Dejavu addon's registrations; every site is checked in memory when a verb
// runs (the addon installs its hooks after the first harness step):
//  - theInterface 007C7D50: screen stack begin/end at +34/+38, the current
//    screen is end[-1] (GetCurrentScreen 005F7590).
//  - CI3DScreenMission (primary vtable 00740764) creates its CI3DObjectSkills
//    (vtable 00742980) at 006355A1 and stores it at +1A0 (006355B2).
//    CI3DObjectSkills: pScreen +148, nSkills +14C, nTypes data +15C, size
//    +160 (ChangeSkill reads +15C/+148 at 0063FBE2; OnLButtonUp tests +14C).
//  - A click on a skill button: OnLButtonUp calls ChangeSkill(slot) 0063FB50
//    at 0063FE2B. The addon hooks that entry; its go-camp button is the last
//    slot of type 2 (RPGMSKL_WAGON) and runs its go-camp request (combat,
//    enemy-radius and zone checks, then GoCamp(IFACE_CAMP)).
//  - CI3DScreenCamp (vtable 0074091C): slot 42 ClickOnItem(item, part)
//    00610AD0 (addon-hooked), whose ISP_CAMPSLOTS case reads pWagon at +1E0
//    (00610B1B); eScreenMode +190, nCurPers +1C8. ISP_WAGON = 1 is a click on
//    a stash item: InventoryToPers -> Wear + NotifyItemsUpdated on the doll.
//  - The stash CI3DObjectInventoryItems (vtable 0074197C): pAllItems +1AC
//    (CSimplyArray: data +0, size +4; FormSortedList 0062131B), fToScroll
//    +174 (OnMouseWheel adds delta/120 at 00621134 through its handler base
//    +94; the wide layout's idle hook turns it into rows). Item typeId +10
//    (PersToInventory 0060FE5A), armour = 3005h.
//  - Base maps (CI3DScreenBase, vtable 007405AC) have no skill bar. A
//    trader's "constr<N>" answer runs, in PlayBriefing at 006065A6:
//    theHat.shops(007AA5D8).GetShop(N) 004A74F0; if found,
//    IWorldServer(007AFEA6).SetConstructorNumber(float N) 00578560, then
//    IWorldClient(007AFEA5).GoCamp(IFACE_CAMP = 5) 00578B00. The camp opens
//    without a shop when constr.current is 0 (CI3DScreenCamp::OnActivate).
constexpr std::uintptr_t kOutfitBaseVtableRva = 0x003405ac;
constexpr std::uintptr_t kOutfitShopsRva = 0x003aa5d8, kOutfitGetShopRva = 0x000a74f0;
constexpr std::uintptr_t kOutfitServerFaceRva = 0x003afea6, kOutfitSetConstructorRva = 0x00178560;
constexpr std::uintptr_t kOutfitClientFaceRva = 0x003afea5, kOutfitGoCampRva = 0x00178b00;
constexpr std::uintptr_t kOutfitInterfaceRva = 0x003c7d50;
constexpr std::uintptr_t kOutfitMissionVtableRva = 0x00340764;
constexpr std::uintptr_t kOutfitSkillsVtableRva = 0x00342980;
constexpr std::uintptr_t kOutfitCampVtableRva = 0x0034091c;
constexpr std::uintptr_t kOutfitStashVtableRva = 0x0034197c;
constexpr std::uintptr_t kOutfitChangeSkillRva = 0x0023fb50;
constexpr std::uintptr_t kOutfitClickOnItemRva = 0x00210ad0;
constexpr std::uint32_t kOutfitArmourType = 0x3005;
constexpr int kOutfitMaxArmour = 4096;
struct OutfitSite { std::uintptr_t rva; std::uint8_t length; std::uint8_t bytes[16]; };
constexpr OutfitSite kOutfitSites[] = {
    {0x001f7590, 16, {0x8b,0x41,0x34,0x8b,0x51,0x38,0x3b,0xc2,0x74,0x06,0x8b,0xca,0x8b,0x41,0xfc,0xc3}},
    {0x002355a1, 5, {0xe8,0x5a,0x9d,0x00,0x00}},
    {0x002355b2, 6, {0x89,0x86,0xa0,0x01,0x00,0x00}},
    {0x0023fe1c, 16, {0x3b,0x86,0xb8,0x00,0x00,0x00,0x7d,0x0c,0x50,0x8b,0xcf,0xc6,0x46,0x08,0x01,0xe8}},
    {0x0023fe2c, 4, {0x20,0xfd,0xff,0xff}},
    {0x0023fbe2, 12, {0x8b,0x96,0x5c,0x01,0x00,0x00,0x8b,0xb6,0x48,0x01,0x00,0x00}},
    {0x00210b1b, 6, {0x8b,0x8e,0xe0,0x01,0x00,0x00}},
    {0x0020fe5a, 8, {0x8b,0x43,0x10,0x3d,0x04,0x30,0x00,0x00}},
    {0x0022131b, 11, {0x8b,0x86,0xac,0x01,0x00,0x00,0x33,0xdb,0x8b,0x48,0x04}},
    {0x002210fd, 6, {0x8d,0x8e,0x6c,0xff,0xff,0xff}},
    {0x00221134, 12, {0xd8,0x86,0xe0,0x00,0x00,0x00,0xd9,0x9e,0xe0,0x00,0x00,0x00}},
    {0x002065a6, 15, {0x57,0xb9,0xd8,0xa5,0x7a,0x00,0xe8,0x3f,0x0f,0xea,0xff,0x85,0xc0,0x74,0x1e}},
    {0x002065b5, 15, {0xdb,0x44,0x24,0x44,0x51,0xb9,0xa6,0xfe,0x7a,0x00,0xd9,0x1c,0x24,0xe8,0x99}},
    {0x002065c4, 15, {0x1f,0xf7,0xff,0x6a,0x05,0xb9,0xa5,0xfe,0x7a,0x00,0xe8,0x2d,0x25,0xf7,0xff}},
};
const char* outfit_contract_error(const std::uint8_t* image) noexcept {
    if (lia_harness()) return "not ported on LiA";
    for (const auto& site : kOutfitSites)
        if (std::memcmp(image + site.rva, site.bytes, site.length) != 0) return "outfitting screen site bytes differ";
    std::uintptr_t click = 0;
    std::memcpy(&click, image + kOutfitCampVtableRva + 42 * 4, sizeof(click));
    if (click != reinterpret_cast<std::uintptr_t>(image) + kOutfitClickOnItemRva)
        return "camp vtable slot 42 is not ClickOnItem 00610AD0";
    return nullptr;
}
using OutfitChangeSkillFn = void(__thiscall*)(void*, int);
using OutfitClickOnItemFn = void(__thiscall*)(void*, void*, int);
// SEH only: no C++ objects in these frames.
using OutfitGetShopFn = void*(__thiscall*)(void*, int);
using OutfitSetConstructorFn = void(__thiscall*)(void*, float);
using OutfitGoCampFn = void(__thiscall*)(void*, int);
// Shop numbers 1..999 that GetShop finds (up to `capacity`); -1: fault.
int outfit_shops(std::uint8_t* image, int* shops, int capacity) noexcept {
    __try {
        int found = 0;
        for (int n = 1; n < 1000 && found < capacity; ++n)
            if (reinterpret_cast<OutfitGetShopFn>(image + kOutfitGetShopRva)(image + kOutfitShopsRva, n)) shops[found++] = n;
        return found;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// The base map's trader answer: 0 opened, 6 no such shop, 5 fault.
int outfit_base_camp(std::uint8_t* image, int shop) noexcept {
    __try {
        if (shop > 0 && !reinterpret_cast<OutfitGetShopFn>(image + kOutfitGetShopRva)(image + kOutfitShopsRva, shop))
            return 6;
        reinterpret_cast<OutfitSetConstructorFn>(image + kOutfitSetConstructorRva)(image + kOutfitServerFaceRva,
                                                                                    static_cast<float>(shop));
        reinterpret_cast<OutfitGoCampFn>(image + kOutfitGoCampRva)(image + kOutfitClientFaceRva, 5);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 5; }
}
// 0 pressed on a mission map, 7: the current screen is a base map's.
int outfit_press_go_camp(std::uint8_t* image, int& slot_out, int& skills_out) noexcept {
    __try {
        std::uint8_t* face = image + kOutfitInterfaceRva;
        const auto begin = *reinterpret_cast<std::uintptr_t*>(face + 0x34);
        const auto end = *reinterpret_cast<std::uintptr_t*>(face + 0x38);
        if (end <= begin || (end - begin) % 4 || end - begin > 64 * 4) return 1;
        auto* top = *reinterpret_cast<std::uint8_t**>(end - 4);
        if (top && *reinterpret_cast<std::uint8_t**>(top) == image + kOutfitBaseVtableRva) return 7;
        if (!top || *reinterpret_cast<std::uint8_t**>(top) != image + kOutfitMissionVtableRva) return 2;
        auto* skills = *reinterpret_cast<std::uint8_t**>(top + 0x1a0);
        if (!skills || *reinterpret_cast<std::uint8_t**>(skills) != image + kOutfitSkillsVtableRva ||
            *reinterpret_cast<std::uint8_t**>(skills + 0x148) != top) return 3;
        const int count = *reinterpret_cast<int*>(skills + 0x14c);
        const int* types = *reinterpret_cast<int**>(skills + 0x15c);
        const int size = *reinterpret_cast<int*>(skills + 0x160);
        skills_out = count;
        if (count < 0 || count > 64 || count > size || (count && !types)) return 3;
        int slot = -1;
        for (int i = 0; i < count; ++i) if (types[i] == 2) slot = i;
        if (slot < 0) return 4;
        slot_out = slot;
        reinterpret_cast<OutfitChangeSkillFn>(image + kOutfitChangeSkillRva)(skills, slot);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 5; }
}
// The stash's armour items (up to kOutfitMaxArmour) and the stash size;
// -1: fault, -2: the stash list differs from the verified layout.
int outfit_stash_armour(std::uint8_t* image, std::uint8_t* camp, void** armour, int& stash) noexcept {
    __try {
        auto* wagon = *reinterpret_cast<std::uint8_t**>(camp + 0x1e0);
        if (!wagon || *reinterpret_cast<std::uint8_t**>(wagon) != image + kOutfitStashVtableRva) return -2;
        auto* all = *reinterpret_cast<std::uint8_t**>(wagon + 0x1ac);
        if (!all) return -2;
        auto** items = *reinterpret_cast<std::uint8_t***>(all);
        stash = *reinterpret_cast<int*>(all + 4);
        if (stash < 0 || stash > 100000 || (stash && !items)) return -2;
        int found = 0;
        for (int i = 0; i < stash && found < kOutfitMaxArmour; ++i)
            if (items[i] && *reinterpret_cast<std::uint32_t*>(items[i] + 0x10) == kOutfitArmourType)
                armour[found++] = items[i];
        return found;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool outfit_click(std::uint8_t* camp, void* item) noexcept {
    __try {
        void* const* table = *reinterpret_cast<void* const* const*>(camp);
        reinterpret_cast<OutfitClickOnItemFn>(table[42])(camp, item, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool outfit_scroll(std::uint8_t* image, std::uint8_t* camp, float rows) noexcept {
    __try {
        auto* wagon = *reinterpret_cast<std::uint8_t**>(camp + 0x1e0);
        if (!wagon || *reinterpret_cast<std::uint8_t**>(wagon) != image + kOutfitStashVtableRva) return false;
        *reinterpret_cast<float*>(wagon + 0x174) += rows;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* g_outfit_armour[kOutfitMaxArmour];
std::string outfit_camp_state(std::uint8_t* image, std::uint8_t* camp) {
    int stash = 0;
    const int count = outfit_stash_armour(image, camp, g_outfit_armour, stash);
    return text_of("mode %d, person %d, stash %d items, %d armour", field<int>(camp, 0x190),
                   field<int>(camp, 0x1c8), stash, count);
}

void harness_camp_frame(void* camp_screen) noexcept {
    std::lock_guard<std::recursive_mutex> guard(harness_lock());
    try { apply_pending_completions(); } catch (...) {}
    State& s = state();
    if (!s.enabled || !s.image) return;
    auto* camp = static_cast<std::uint8_t*>(camp_screen);
    if (!camp || field<std::uint8_t*>(camp, 0) != s.image + kOutfitCampVtableRva) return;
    ++s.camp_frames;
    s.camp_idle_presents = 0;
    if (s.queue.empty()) return;
    Entry& entry = s.queue.front();
    const HarnessCommand& command = entry.command;
    if (command.verb == HarnessVerb::open_outfitting) {
        try { s.camp_detail = outfit_camp_state(s.image, camp); } catch (...) {}
        return;
    }
    if (!s.camp_pending) return;
    if (command.verb != HarnessVerb::outfit_equip && command.verb != HarnessVerb::scroll_items) return;
    // Steps count across commands, so consecutive lines keep moving on.
    static std::uint32_t session_equips = 0, applied = 0, refused = 0;
    const auto between = static_cast<unsigned>(command.number[0]);
    if (entry.progress == 0) { s.camp_ok = true; s.camp_detail.clear(); }
    try {
        if (const char* error = outfit_contract_error(s.image)) {
            s.camp_ok = false; s.camp_detail = error;
        } else if (between && entry.progress % between == 0) {
            if (command.verb == HarnessVerb::scroll_items) {
                const bool ok = outfit_scroll(s.image, camp, command.number[1]);
                s.camp_ok = s.camp_ok && ok;
                s.camp_detail = ok ? text_of("%u wheel notches (%+d row each)", entry.progress / between + 1,
                                             static_cast<int>(command.number[1]))
                                   : "stash list not found";
            } else {
                int stash = 0;
                const int count = outfit_stash_armour(s.image, camp, g_outfit_armour, stash);
                if (count <= 0) {
                    s.camp_ok = false;
                    s.camp_detail = count < 0 ? "stash list differs or faulted" : "no armour in the stash";
                } else {
                    // A stride of 7 walks every stash item for most stash
                    // sizes and changes the slot from one step to the next.
                    void* item = g_outfit_armour[(session_equips * 7u) % static_cast<unsigned>(count)];
                    ++session_equips;
                    if (!outfit_click(camp, item)) { s.camp_ok = false; s.camp_detail = "ClickOnItem faulted"; }
                    else {
                        int after_stash = 0;
                        const int after = outfit_stash_armour(s.image, camp, g_outfit_armour, after_stash);
                        bool still = false;
                        for (int i = 0; i < after; ++i) still = still || g_outfit_armour[i] == item;
                        ++(still ? refused : applied);
                        s.camp_detail = text_of("equip %u: %u worn, %u refused by the screen; stash %d items, %d armour",
                                                session_equips, applied, refused, after_stash, after);
                    }
                }
            }
        }
    } catch (...) { s.camp_ok = false; s.camp_detail = "allocation failed"; }
    ++entry.progress;
    s.camp_done = true;
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
        const bool lia_verb = command.verb == HarnessVerb::console || command.verb == HarnessVerb::list_saves;
        s.world_pending = false;
        s.world_done = true;
        s.world_ok = supported_world || (lia_world && lia_verb);
        s.world_detail = lia_world && !lia_verb ? "not ported on LiA" :
                         "Draw target is not the static CWorldClient";
        if (supported_world && command.verb == HarnessVerb::camera_path) {
            s.hero_follow = false;
            // Temporary: the start pose at the first step, then the pose
            // interpolated linearly (yaw along the shorter arc).
            std::uint8_t* camera = s.image + kCameraRva;
            const std::string off_map = entry.progress == 0 ?
                camera_off_map(command.number[0], command.number[1]) : std::string();
            if (!off_map.empty()) {
                // The path runs straight from an on-map pose, so an on-map
                // target keeps every step on the map.
                entry.progress = command.frames;
                s.world_ok = false;
                s.world_detail = off_map;
            } else {
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
                // Yaw along the shorter arc; pitch from its equivalent nearest the
                // target (a loaded save can hold 45 rad, read as 2578 degrees:
                // without the wrap the camera turned about seven vertical circles in
                // 600 frames and brought all ground around it into view).
                for (int i = 0; i < 5; ++i) {
                    float delta = command.number[i] - s.path_start[i];
                    if (i == 2) delta = std::remainder(delta, 360.0f);
                    if (i == 3) { step.number[i] = command.number[i] - std::remainder(delta, 360.0f) * (1.0f - t); continue; }
                    step.number[i] = s.path_start[i] + delta * t;
                }
                apply_camera(step);
                s.world_detail = text_of("step %u/%u; ", entry.progress, command.frames) + camera_state();
            }
        } else if (supported_world && command.verb == HarnessVerb::equip_cycle) {
            // The last step's result stands until the next step.
            static std::string step_detail;
            static bool step_ok = true;
            // Steps count across commands, so consecutive equip_cycle lines
            // keep producing new sets instead of repeating the first ones.
            static std::uint32_t session_steps = 0;
            const auto between = static_cast<unsigned>(command.number[0]);
            if (entry.progress == 0) { step_detail.clear(); step_ok = true; }
            if (between && entry.progress % between == 0)
                step_ok = bridge_harness_equip_step(session_steps++, step_detail) && step_ok;
            s.world_ok = step_ok;
            s.world_detail = step_detail;
            ++entry.progress;
        } else if (supported_world && command.verb == HarnessVerb::open_outfitting) {
            if (const char* error = outfit_contract_error(s.image)) { s.world_ok = false; s.world_detail = error; }
            else {
                int slot = -1, skills = 0;
                static const char* const kWhy[] = {"pressed", "interface screen stack unreadable",
                    "the current screen is neither the mission nor the base screen (a dialog or menu is open)",
                    "mission skill bar differs from the verified layout",
                    "no go-camp button on the skill bar (select one own hero; JMVGoCamp off?)",
                    "call faulted", "no such shop"};
                const int shop = static_cast<int>(command.frames);
                int result = outfit_press_go_camp(s.image, slot, skills);
                if (result == 7) {
                    int shops[16]{};
                    const int count = outfit_shops(s.image, shops, 16);
                    std::string list;
                    for (int i = 0; i < count; ++i) list += text_of(i ? ",%d" : "%d", shops[i]);
                    result = outfit_base_camp(s.image, shop);
                    s.world_detail = text_of("base map: trader answer constr%d %s (shops found: %s)", shop,
                                             kWhy[result], list.empty() ? "none" : list.c_str());
                } else {
                    s.world_detail = text_of("go-camp button %s (slot %d of %d)", kWhy[result], slot, skills);
                }
                s.world_ok = result == 0;
            }
        } else if (supported_world && command.verb == HarnessVerb::pause) {
            entry.progress = 0;
            request_pause(command.frames != 0);
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
        } else if (supported_world && command.verb == HarnessVerb::spawn) {
            s.world_detail = apply_spawn(command, s.world_ok);
        } else if (supported_world && command.verb == HarnessVerb::drawall) {
            std::int32_t before = 0;
            const bool on = command.frames != 0;
            const std::int32_t value = on ? 0 : (s.drawall_saved ? s.drawall_previous : -1);
            s.world_ok = bridge_harness_draw_visible_only(value, before);
            if (!s.world_ok) s.world_detail = "DrawVisibleOnly unreadable (no game image)";
            else {
                if (on && !s.drawall_saved) { s.drawall_saved = true; s.drawall_previous = before; }
                std::int32_t now = 0;
                bridge_harness_draw_visible_only(-1, now);
                s.world_detail = text_of("DrawVisibleOnly=%d (was %d)%s", now, before,
                                         !on && value < 0 ? "; drawall on had not changed it" : "");
                if (!on) s.drawall_saved = false;
            }
        } else if (supported_world && command.verb == HarnessVerb::camera_unit) {
            s.hero_follow = false;
            camera_unit_step(command);
        } else if (supported_world && command.verb == HarnessVerb::camera) {
            s.hero_follow = false;
            s.world_detail = camera_off_map(command.number[0], command.number[1]);
            s.world_ok = s.world_detail.empty();
            if (s.world_ok) {
                apply_camera(command);
                s.world_detail = camera_state();
            }
        } else if (supported_world && command.verb == HarnessVerb::camera_hero) {
            s.world_detail = hero_contract_error();
            s.world_ok = s.world_detail.empty();
            if (s.world_ok) {
                if (!std::isnan(command.number[4])) apply_camera(command);
                std::int32_t id = 0;
                const char* error = follow_party_leader(id);
                s.world_ok = error == nullptr;
                s.hero_follow = s.world_ok;
                s.hero_epoch = bridge_current_world_epoch();
                s.world_detail = error ? error : text_of("following party leader id=%d; ", id) + camera_state();
            }
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
    if (s.hero_follow && supported_world) {
        // Native input scrolling detaches the carrier. While this harness
        // mode is active, reassert the CURRENT leader through SetCarrier;
        // camera/path commands or a world change release the mode.
        if (s.hero_epoch != bridge_current_world_epoch()) s.hero_follow = false;
        else {
            std::int32_t id = 0;
            if (const char* error = follow_party_leader(id)) {
                s.hero_follow = false;
                bridge_append_probe_event("harness_camera_hero_failed", error);
            }
        }
    }
    if (s.clock_pinned && supported_world) apply_clock(world);
}

} // namespace owned
