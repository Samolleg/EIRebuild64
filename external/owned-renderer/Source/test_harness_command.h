#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace owned {

// One line of the file-driven test harness (EI_RENDERER_HARNESS_DIR).
enum class HarnessVerb : std::uint8_t {
    invalid,
    camera,        // camera x y yaw pitch zoom   (world units, degrees, distance)
    state,         // camera pose and client hour, as a `camera` line can replay
    clock_pin,     // clock <hour>
    clock_release, // clock release
    wait,          // wait <presents>
    capture,       // the existing F11 capture (composed frame)
    profile,       // profile [seconds]: the existing outer frame gap profile
    screenshot,    // screenshot <name>: back buffer to <dir>\<name>.bmp
    dump_units,    // the existing Local\EID3D11_DumpUnits request
    config,        // config <key> <value>: F10 option, overlay or debug_view
    load_save,     // load_save <dialog name, slot, or slot file>
    list_saves,    // the Load dialog's save directories and display names
    console,       // console <text>: the game's console parser; walk <x> <y> issues @MoveToPoint(GetLeader(),x,y)
    quit,          // the game's own QuitProgram
    pause,         // pause on|off: the game's mission pause, set (not toggled)
    godmode,       // godmode on|off: immediate protection of the local party
    wait_ready,    // wait_ready [seconds]: loaded world fully prepared by the renderer
    main_menu,     // the game's own return to the main menu, from any screen
    note,          // note <text>: the harness banner ('|' starts a line) and window title
    // Temporary (camera-movement frame cost, 2026-10-11; remove once found):
    // camera_path <frames> x y yaw dist pitch: the camera moves from its pose
    // to the target over that many world frames, one step per present, and
    // each present's timing goes to <dir>\camera_path.jsonl.
    camera_path,
    // equip_cycle <steps> <presents_between>: every <presents_between> world
    // frames the first hero's appearance wears a new armour set drawn from
    // the armour the world's units wear (SetArmors + NotifyItemsUpdated, the
    // inventory-update path that assembles the character atlases). Visual
    // only; the inventory does not change. Equipment-load stress.
    equip_cycle,
    // Outfitting-screen stress (2026-11-06), through the game's own screen calls:
    // open_outfitting [shop]: on a mission map the Dejavu addon's go-camp
    // skill button (the same ChangeSkill call a click makes); on a base map
    // what a trader's "constr<shop>" dialog answer runs (SetConstructorNumber,
    // GoCamp); then waits for the camp screen.
    // outfit_equip <steps> <presents_between>: every <presents_between> camp
    // frames, the screen's own ClickOnItem(stash armour, ISP_WAGON), as a
    // click on a stash item: the doll wears it (InventoryToPers).
    // scroll_items <steps> <presents_between>: every <presents_between> camp
    // frames one wheel notch on the stash list (signed: + scrolls down).
    open_outfitting,
    outfit_equip,
    scroll_items,
    // camera_unit <id> <yaw> <pitch> <distance>: the camera pivot and game
    // camera distance corrected, one step a world view, until the unit's
    // position (the id dump_units lists) projects to the screen centre at
    // that yaw and pitch with the eye <distance> metres from it. @x,y in
    // place of the id: the unit nearest that world position (within 6 m),
    // for spawned units whose id differs every run.
    // id: frames (as int32); yaw pitch distance: number[0..2].
    camera_unit,
    // drawall on|off: the game's DrawVisibleOnly setting off (every unit is
    // drawn and published, not only those the party sees) / back to the
    // value the first drawall on found. frames: 1 on, 0 off.
    drawall,
    // Append verbs: existing ordinals are also used by the x64 host wire.
    camera_hero,   // camera hero [yaw pitch zoom]: native party-leader follow
    expect_godmode,
    expect_hero_in_view,
    expect_event_absent,
    expect_event_present,
    expect_value,  // expect value <diagnostic name> <op> <number>
    // spawn <prototype | "prototype with spaces"> <x> <y> [yaw=deg] [player=N]
    //       [model=mask] [prim=texture] [sec=texture]: a unit placed as the
    // map loader places one (a CMEUnit through CWorldServer::AddMapObject).
    // name: prototype; number[0..2]: x y yaw; frames: player;
    // value: "model\nprim\nsec" (empty: the database default).
    spawn,
};

struct HarnessCommand {
    HarnessVerb verb = HarnessVerb::invalid;
    float number[5]{};   // camera: x y yaw pitch zoom; clock: hour; profile: seconds
    std::uint32_t frames = 0;  // wait: presents; pause/godmode: 1 on, 0 off; wait_ready: seconds
    std::string name;    // screenshot stem, config key, save selector or console text
    std::string value;   // config value; expect value: the operator (== != < <= > >=)
    std::string error;   // why an invalid line was rejected
    double expected = 0; // expect value: the parsed finite number
};

// Blank lines and lines starting with '#' are skipped without a result.
bool harness_line_skipped(std::string_view line) noexcept;

// Pure and locale independent: the whole command contract lives here.
HarnessCommand parse_harness_command(std::string_view line);

// A requested load completes on the first successful world draw that began
// after the new world epoch was observed with no transition pending. Inputs
// are the lifecycle epoch and the monotonic count of completed world draws;
// the per-frame draw snapshot is already cleared at EndScene when the harness
// steps at Present, so it cannot be the completion signal.
struct HarnessLoadWait {
    std::uint64_t request_epoch = 0;
    bool new_epoch_seen = false;
    std::uint64_t draws_at_new_epoch = 0;
};
bool harness_load_completed(HarnessLoadWait& wait, std::uint64_t epoch,
                            std::uint64_t completed_draws, bool transition_pending) noexcept;

// The game's startup transition into the main menu (no party zone) can
// replace a load requested just before it began; the load then "completes"
// in the menu. Such a load is requested again, once. True means request again.
bool harness_load_retry(bool zone_matches, bool loaded_zone_empty, bool& retried) noexcept;

// Wall-clock limits for commands that wait on the game or the renderer.
// The ticks are GetTickCount64 milliseconds; a limit of N seconds expires
// once N seconds have elapsed since the command started.
constexpr std::uint32_t kHarnessLoadTimeoutSeconds = 120;
constexpr std::uint32_t kHarnessReadyDefaultSeconds = 60;
bool harness_timed_out(std::uint64_t start_ms, std::uint64_t now_ms,
                       std::uint32_t seconds) noexcept;

} // namespace owned
