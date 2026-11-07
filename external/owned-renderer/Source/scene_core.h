#pragma once
// Core mode (EI_RENDERER_X64_CORE=1, phase 2 step S3): the command ids of
// the Op::scene stream. The x86 game process sends each typed scene command
// as one message (x64remote::scene_message); the host decodes it with
// scene_wire and runs it on its Renderer on the replay thread, the single
// owner of the host's immediate context. Ids are part of the wire
// (x64_remote_protocol.h kVersion): append, never renumber.
//
// Message layout: u8 envelope flag, then (flag 1) the frame envelope (the
// bridge frame values and the game thread's widescreen::hud_blit at the
// post), then the command's fields. Flag 0: the host keeps the last
// envelope it installed (the values did not change since). Commands whose
// frame_values is false never carry one.
//
// An id the host does not know, or a message that does not decode to
// exactly its bytes, loses the device with the command's name: there is no
// partial execution and no fallback.
#include <cstdint>

namespace owned {
namespace scene_core {

// X(name, struct): every typed command the host runs. The struct has
// fields() (scene_core_types.h) and run(Renderer&).
#define SCENE_CORE_COMMANDS(X)                                   \
    X(begin_engine_frame, ::owned::scene_cmd::BeginEngineFrame)  \
    X(begin_scene, ::owned::scene_cmd::BeginScene)               \
    X(end_scene, ::owned::scene_cmd::EndScene)                   \
    X(flush_native, ::owned::scene_cmd::FlushNative)             \
    X(composite_world, ::owned::scene_cmd::CompositeWorld)       \
    X(clear, ::owned::scene_cmd::Clear)                          \
    X(present, ::owned::scene_core::Present)                     \
    X(config, ::owned::scene_core::Config)                       \
    X(mark_load_picture, ::owned::scene_cmd::MarkLoadPicture)    \
    X(surface_write, ::owned::scene_ui::SurfaceWrite)            \
    X(blits, ::owned::scene_ui::Blits)                           \
    X(present_image, ::owned::scene_ui::PresentImage)            \
    X(draw_frame, ::owned::scene_ui::DrawFrame)                  \
    X(cursor, ::owned::scene_ui::Cursor)                         \
    X(retire_surface, ::owned::scene_ui::RetireSurface)          \
    X(flush_ui, ::owned::scene_ui::FlushUi)                      \
    X(screen_rectangle, ::owned::scene_ui::ScreenRectangle)      \
    X(hud_frame_layer, ::owned::scene_ui::HudFrameLayer)          \
    X(game_particles, ::owned::scene_cmd::GameParticles)          \
    X(game_ribbon, ::owned::scene_cmd::GameRibbon)                \
    X(game_selection, ::owned::scene_cmd::GameSelection)          \
    X(game_glyphs, ::owned::scene_cmd::GameGlyphs)                \
    X(game_print_run, ::owned::scene_cmd::GamePrintRun)           \
    X(world_lighting, ::owned::scene_cmd::WorldLighting)          \
    X(discard_native, ::owned::scene_cmd::DiscardNative)          \
    X(take_native_checkpoint, ::owned::scene_cmd::TakeNativeCheckpoint)\
    X(release_native_checkpoint, ::owned::scene_cmd::ReleaseNativeCheckpoint)\
    X(rollback_native, ::owned::scene_cmd::RollbackNative)        \
    X(abort_directional_shadows, ::owned::scene_cmd::AbortDirectionalShadows)\
    X(abort_point_shadow_faces, ::owned::scene_cmd::AbortPointShadowFaces)\
    X(scene_packet, ::owned::scene_ui::Packet)                    \
    X(world_begin, ::owned::scene_core::WorldBegin)               \
    X(world_end, ::owned::scene_core::WorldEnd)                   \
    X(world_scene, ::owned::scene_core::WorldScene)               \
    X(transparent_terrain, ::owned::scene_core::TransparentTerrain)\
    X(wound_layer, ::owned::scene_core::WoundLayer)               \
    X(terrain_atlases, ::owned::scene_core::TerrainAtlases)       \
    X(neighbour_begin, ::owned::scene_core::NeighbourBegin)       \
    X(neighbour_stop, ::owned::scene_core::NeighbourStop)         \
    X(game_status, ::owned::scene_core::GameStatus)               \
    X(resize, ::owned::scene_core::Resize)                        \
    X(readback, ::owned::scene_core::Readback)                    \
    X(capture, ::owned::scene_ui::Capture)                        \
    X(camp_decoration, ::owned::scene_ui::CampDecorationFrame)    \
    X(begin_camp_popup, ::owned::scene_ui::BeginCampPopup)        \
    X(end_camp_popup, ::owned::scene_ui::EndCampPopup)            \
    X(reset_camp_popup, ::owned::scene_ui::ResetCampPopup)        \
    X(glyph_atlas, ::owned::scene_ui::GlyphAtlas)                 \
    X(character_pane_text, ::owned::scene_ui::CharacterPaneText)  \
    X(capture_request, ::owned::scene_core::CaptureRequest)       \
    X(progress_frames, ::owned::scene_core::ProgressFrames)       \
    X(widescreen_state, ::owned::scene_core::WidescreenState)     \
    X(preview, ::owned::scene_core::Preview)                      \
    X(resource_mounts, ::owned::scene_core::ResourceMounts)       \
    X(combat_state, ::owned::scene_core::CombatState)             \
    X(invalidate_camp_popup, ::owned::scene_core::InvalidateCampPopup)\
    X(display_apply_result, ::owned::scene_core::DisplayApplyResult)\
    /* ---- camp icons and camp textures (claude/s3-store) ---- */ \
    X(camp_icon_mounts, ::owned::scene_core::CampIconMounts)      \
    X(camp_icon_request, ::owned::scene_core::CampIconRequest)    \
    X(camp_icon_priority, ::owned::scene_core::CampIconPriority)  \
    X(camp_icon_bind, ::owned::scene_core::CampIconBind)          \
    X(retire_camp_texture, ::owned::scene_ui::RetireCampTexture)  \
    X(hit_test_frame, ::owned::scene_cmd::HitTestFrame)           \
    X(map_preview_activate, ::owned::scene_core::MapPreviewActivate) \
    X(map_preview_retire, ::owned::scene_core::MapPreviewRetire)  \
    X(map_preview_draw, ::owned::scene_core::MapPreviewDraw)    \
    X(map_preview_precache, ::owned::scene_core::MapPreviewPrecache) \
    X(map_marker_begin, ::owned::scene_core::MapMarkerBegin) \
    X(map_marker_end, ::owned::scene_core::MapMarkerEnd) \
    X(map_markers_draw, ::owned::scene_core::MapMarkersDraw)

enum class Command : std::uint16_t {
    none = 0,
    layout = 1,  // scene_wire::encode_layouts(): the POD layout check, first message
#define SCENE_CORE_ID(name, type) name,
    SCENE_CORE_COMMANDS(SCENE_CORE_ID)
#undef SCENE_CORE_ID
    count
};

inline const char* command_name(std::uint16_t id) noexcept {
    switch (static_cast<Command>(id)) {
    case Command::layout: return "layout";
#define SCENE_CORE_NAME(name, type) case Command::name: return #name;
    SCENE_CORE_COMMANDS(SCENE_CORE_NAME)
#undef SCENE_CORE_NAME
    default: return "unknown";
    }
}

// The id of a command type; none for a type core mode cannot send yet
// (its post is refused, logged and its visual omitted).
template <class C> struct wire_command { static constexpr Command id = Command::none; };

}  // namespace scene_core
}  // namespace owned
