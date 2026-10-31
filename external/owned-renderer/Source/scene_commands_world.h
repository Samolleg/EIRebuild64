#pragma once
// Typed world, native-queue and effect commands of the scene thread
// (core split S2b; RENDERER_ARCHITECTURE.md "Scene thread").
//
// One struct per game-thread post site. A command holds values only: copied
// arrays, fixed-function state, ids. It never holds the Renderer, a game
// pointer or a D3D object; the executor supplies the Renderer to run().
// Standard mode posts it through Renderer::scene_post_command onto the same
// in-process queue the lambdas used, and run() calls the same Renderer code,
// so behaviour is unchanged. A later encoder can serialise these fields.
//
// Each command names its pass and the targets it may write (pass/writes),
// as the frame plan of a native D3D12 executor will need them. Where the
// D3D11 path still draws into the colour target bound at its stream position
// (world colour until composite_world, the interface target after it), the
// command says so with target::stream_color instead of hiding it; that is a
// remaining D3D11 assumption, not a resolved one.
//
// Included by renderer.h after FixedState and SurfaceImage are defined; the
// run() bodies live next to the Renderer code they call.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace owned {

class Renderer;

namespace scene_cmd {

enum class Pass : std::uint8_t {
    frame_begin,         // ends the previous engine frame, binds world colour/depth
    scene_begin,         // per-D3D7-scene settings; shadow/point maps may be reallocated
    scene_end,           // shadow history commit, profiler pass end
    world_native,        // the queued native geometry (or an open shadow collection)
    world_composite,     // world image resolved into the interface target
    world_lighting,      // shared world light constant (flushes the work that used the old one)
    game_effects,        // particles and lightning ribbons at their stream position
    ground_marks,        // selection marker and terrain prints (deferred past water)
    combat_text,         // screen-space glyphs
    native_queue_state,  // CPU queue bookkeeping (checkpoint, rollback, discard)
    directional_shadows, // directional shadow collection
    point_shadows,       // point shadow cube collection
    clear,               // fixed-function Clear of the bound targets
};

namespace target {
enum : std::uint32_t {
    none = 0,
    world_color = 1u << 0,        // the scene composite's world target
    world_depth = 1u << 1,        // world_dsv_
    interface_color = 1u << 2,    // the interface/back buffer target
    stream_color = 1u << 3,       // whichever of the two is bound at the stream position
    stream_depth = 1u << 4,       // the depth bound at the stream position
    glow = 1u << 5,               // bloom glow (additive world particles)
    particle_depth = 1u << 6,     // reflection particle depth pair
    directional_shadow = 1u << 7, // directional shadow maps (open collection)
    point_shadow = 1u << 8,       // point shadow atlas (open collection)
    shadow_history = 1u << 9,     // directional shadow history
};
}  // namespace target

// The surface a command samples. Until surface ids exist (S2e) this is the
// game thread's pixel-free GPU snapshot (Renderer::scene_surface).
using SurfaceHandle = std::shared_ptr<SurfaceImage>;

// ---- Frame skeleton --------------------------------------------------------
struct BeginEngineFrame {  // begin_engine_frame, queued (world epoch unchanged)
    static constexpr Pass pass = Pass::frame_begin;
    static constexpr std::uint32_t writes = target::world_color | target::world_depth | target::interface_color;
    static constexpr bool frame_values = true;
    // 0 in standard scene mode (a queued begin never releases). Core mode: the
    // game thread's lifecycle epoch every frame, so the host releases world
    // state at the first frame of a new world once its own draws allow it.
    std::uint64_t world_epoch = 0;
    void run(Renderer& renderer);
};
// The global map's zone hit-test frame (#200): begin before
// CI3DScreenGlobalMap::GenerateHitTestDDS draws it, end after its capture.
// Between them the world draws flat default colours into the lossless
// hit-test target and the game's capture reads that target.
struct HitTestFrame {
    static constexpr Pass pass = Pass::frame_begin;
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth;
    static constexpr bool frame_values = true;
    bool begin = false;
    void run(Renderer& renderer);
};
// The load reveal keeps a copy of this frame (mark_load_picture).
struct MarkLoadPicture {
    static constexpr Pass pass = Pass::native_queue_state;
    static constexpr std::uint32_t writes = target::none;
    static constexpr bool frame_values = false;
    void run(Renderer& renderer);
};
struct BeginScene {  // the renderer half of begin_scene
    static constexpr Pass pass = Pass::scene_begin;
    static constexpr std::uint32_t writes = target::world_color | target::world_depth;  // a settings change flushes
    static constexpr bool frame_values = true;
    void run(Renderer& renderer);
};
struct EndScene {
    static constexpr Pass pass = Pass::scene_end;
    static constexpr std::uint32_t writes = target::shadow_history;
    static constexpr bool frame_values = true;
    void run(Renderer& renderer);
};
struct FlushNative {
    static constexpr Pass pass = Pass::world_native;
    // Also the open directional/point/rain collection when one is open.
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth;
    static constexpr bool frame_values = true;
    unsigned layout_slot = 0;
    void run(Renderer& renderer);
};
struct CompositeWorld {
    static constexpr Pass pass = Pass::world_composite;
    static constexpr std::uint32_t writes = target::world_color | target::world_depth | target::interface_color;
    static constexpr bool frame_values = true;
    void run(Renderer& renderer);
};
struct WorldLighting {
    static constexpr Pass pass = Pass::world_lighting;
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth;  // flush before the change
    static constexpr bool frame_values = true;
    NativeWorldLighting lighting{};
    WorldEnvironment environment{};  // read on the game thread at the call
    void run(Renderer& renderer);
};
struct Clear {
    static constexpr Pass pass = Pass::clear;
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth;
    static constexpr bool frame_values = true;
    std::vector<D3DRECT> rects;  // already placed for the camp layout
    DWORD flags = 0;
    D3DCOLOR color = 0;
    float z = 0.0f;
    DWORD stencil = 0;
    void run(Renderer& renderer);
};

// ---- Game effects ------------------------------------------------------------
struct GameParticles {
    static constexpr Pass pass = Pass::game_effects;
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth | target::glow | target::particle_depth;
    static constexpr bool frame_values = true;
    GameParticleView view{};  // particles/count null: the copy below is the data
    std::vector<GameParticle> particles;
    FixedState state;
    SurfaceHandle texture;
    void run(Renderer& renderer);
};
struct GameRibbon {
    static constexpr Pass pass = Pass::game_effects;
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth | target::glow | target::particle_depth;
    static constexpr bool frame_values = true;
    GameRibbonView view{};  // points/count null: the copy below is the data
    std::vector<GameRibbonPoint> points;
    FixedState state;
    SurfaceHandle texture;
    void run(Renderer& renderer);
};
struct GameSelection {
    static constexpr Pass pass = Pass::ground_marks;
    static constexpr std::uint32_t writes = target::world_color | target::world_depth;
    static constexpr bool frame_values = true;
    GameSelectionView view{};
    FixedState state;
    SurfaceHandle texture;
    void run(Renderer& renderer);
};
struct GameGlyphs {
    static constexpr Pass pass = Pass::combat_text;
    static constexpr std::uint32_t writes = target::stream_color | target::stream_depth;
    static constexpr bool frame_values = true;
    std::vector<GameParticleGpu> glyphs;
    FixedState state;
    SurfaceHandle texture;
    void run(Renderer& renderer);
};
struct GamePrintRun {
    static constexpr Pass pass = Pass::ground_marks;
    static constexpr std::uint32_t writes = target::world_color | target::world_depth;
    static constexpr bool frame_values = true;
    std::vector<GameParticleGpu> vertices;
    std::array<float, 16> object_to_clip{};
    float clip_half_width = 0.0f, clip_half_height = 0.0f;
    FixedState state;
    SurfaceHandle texture;
    void run(Renderer& renderer);
};

// ---- Native queue state --------------------------------------------------------
// Checkpoints are named by a game-thread id; the scene thread keeps the real
// checkpoint under it (Renderer::scene_checkpoints_).
struct DiscardNative {
    static constexpr Pass pass = Pass::native_queue_state;
    static constexpr std::uint32_t writes = target::none;
    static constexpr bool frame_values = false;
    void run(Renderer& renderer);
};
struct TakeNativeCheckpoint {
    static constexpr Pass pass = Pass::native_queue_state;
    static constexpr std::uint32_t writes = target::none;
    static constexpr bool frame_values = false;
    std::uint64_t checkpoint_id = 0;
    void run(Renderer& renderer);
};
struct ReleaseNativeCheckpoint {
    static constexpr Pass pass = Pass::native_queue_state;
    static constexpr std::uint32_t writes = target::none;
    static constexpr bool frame_values = false;
    std::uint64_t checkpoint_id = 0;
    void run(Renderer& renderer);
};
struct RollbackNative {  // an unknown id discards the unqueued work, as before
    static constexpr Pass pass = Pass::native_queue_state;
    static constexpr std::uint32_t writes = target::none;
    static constexpr bool frame_values = false;
    std::uint64_t checkpoint_id = 0;
    void run(Renderer& renderer);
};

// ---- Shadow collection aborts (the device's per-EndScene sink teardown) --------
struct AbortDirectionalShadows {
    static constexpr Pass pass = Pass::directional_shadows;
    static constexpr std::uint32_t writes = target::none;  // discards the collection
    static constexpr bool frame_values = false;
    void run(Renderer& renderer);
};
struct AbortPointShadowFaces {
    static constexpr Pass pass = Pass::point_shadows;
    static constexpr std::uint32_t writes = target::none;  // discards the collection
    static constexpr bool frame_values = false;
    void run(Renderer& renderer);
};

}  // namespace scene_cmd
}  // namespace owned
