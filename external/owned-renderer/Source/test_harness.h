#pragma once

#include "test_harness_command.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace owned {

// File-driven test harness. EI_RENDERER_HARNESS_DIR names a folder; without it
// every entry point returns at once and nothing in the game is touched.
// Once a present the harness stats <dir>\cmd.txt; when present it is read,
// deleted and its lines run in order, one result line each appended to
// <dir>\results.txt as "<id>\t<done|failed>\t<command>\t<detail>".
// Entry points run on the game thread (present and CWorldClient::Draw); with
// the render_thread option harness_complete also runs on the scene thread.
bool harness_enabled() noexcept;
const std::string& harness_directory() noexcept;
// camera_path (temporary): the steps the front command has applied (0 when
// the front is not a camera_path).
unsigned harness_camera_path_steps() noexcept;
// The route banner set by the note verb (empty: none).
std::string harness_banner_text();

// Present step. Runs the harness-owned commands (wait, state, dump_units,
// quit, load_save with its 120 s limit, main_menu, immediate party godmode/
// expect godmode, event-log expectations, and the hand-off of camera/clock/
// pause/console to the next world frame) and returns the
// renderer-owned command at the front of the queue (capture, profile,
// screenshot, config, wait_ready, expect value and event barriers), or nullptr while idle or blocked. The same command is
// returned on every present until harness_complete() retires it.
const HarnessCommand* harness_present_step() noexcept;
void harness_complete(bool ok, const std::string& detail) noexcept;
// Scene thread (render_thread option): the game thread runs the present step
// and copies the renderer verb it returned (false: none), with the
// camera_path progress and the travel-map state at that moment; the scene
// thread then runs the verb and completes it. Entry points that touch the
// queue are locked against each other.
bool harness_present_step_copy(HarnessCommand& command, unsigned long& entry_id,
                               unsigned& camera_steps, bool& on_travel_map_now) noexcept;
// Retires the front command only while it is still entry_id (the game thread
// may already have stepped the same command again for the next frame).
void harness_complete_entry(unsigned long entry_id, bool ok, const std::string& detail) noexcept;

// CWorldClient::Draw entry, before the original body: applies a pending
// camera pose, pause request or console text, maintains camera hero follow and a pinned clock
// (AdjustLightingWithTime runs inside Draw, after this point). Save completion
// is observed at present, after a successful Draw in the new world epoch.
void harness_world_frame(void* world_client) noexcept;
// CI3DScreenCamp::Draw entry (camp_inventory's hook), before the original
// body: runs a pending outfit_equip or scroll_items step on that screen.
void harness_camp_frame(void* camp_screen) noexcept;
// Debug lightning strike at the camera focus (the rain's own strike script); the F10
// Diagnostics button requests it, the next world frame strikes.
void request_debug_lightning() noexcept;
// Keeps camera projection clipping beyond the renderer's independent draw distance.
void set_game_effect_distance(float distance) noexcept;
void game_back_clip_world_frame(void* world_client) noexcept;
// DirectDrawCreate: set the fixed far plane before the game builds its projection.
void apply_initial_game_back_clip() noexcept;
void debug_lightning_world_frame(void* world_client) noexcept;
// Load reveal pause (not a harness command; runs without the harness). At the
// CWorldClient::Draw entry: while hold is true for a new world epoch of a
// local single-player mission, pauses the game with the KA_PAUSE setter the
// harness pause verb uses; when hold ends, resumes only a pause it set itself.
// A pause the player or the save already had is left alone. reporting false:
// the renderer stopped reporting its hold, and a pause set here is released.
// previous_draw_counted: the previous world Draw completed (the hold counts
// only those); 30 uncounted Draws in a row release a pause set here too.
void load_pause_world_frame(void* world_client, bool hold, std::uint64_t epoch,
                            bool single_player_mission, bool reporting,
                            bool previous_draw_counted) noexcept;

// camera_unit: the units' world positions (dump_units' source, the world's
// visual frame) and the world-to-clip matrix of one world frame, given on the
// game thread when that frame's world scene is queued. Wanted only while a
// camera_unit command runs.
struct HarnessUnitPosition { std::int32_t id; float position[3]; };
bool harness_wants_world_view() noexcept;
void harness_note_world_view(std::vector<HarnessUnitPosition> units,
                             const std::array<float, 16>& world_to_clip) noexcept;

// True while the server runs the world/travel map interface, which draws no
// world frames; commands that need the world fail at once there.
bool harness_on_travel_map() noexcept;

} // namespace owned
