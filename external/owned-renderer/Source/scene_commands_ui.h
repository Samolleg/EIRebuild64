#pragma once
// Typed UI scene commands (phase 2 step S2a; PLAN.md 1.2 `ui` and `packet`).
//
// Each struct is the value payload of one UI-side post: what the game thread
// hands the scene thread for blits, presented pictures, the cursor, the
// character pane, the camp popup and decoration, captures, surface
// retirement, the UI flush, screen rectangles, legacy draw frames and generic
// scene packets. A payload holds values only: no game pointers, no
// shared_ptr identity and no D3D objects. Surfaces are named by
// SurfaceHandle, glyph atlases by GlyphAssetKey.
//
// Standard mode runs the payload in-process on the same queue
// (scene_post / scene_post_frame), so the work is unchanged. What keeps a
// named GPU home or glyph atlas alive until the command ran is the separate
// Residency object queued next to the payload; the command resolves every
// handle through it. In core mode (S3) the host's surface table answers the
// same handles and no Residency exists.
//
// Per-frame inputs that are not in a payload travel with the post envelope:
// scene_post_frame captures the bridge frame values and the game thread's
// widescreen::hud_blit (RENDERER_ARCHITECTURE.md, hud_blit landmine) and
// installs both around the work.
#include "renderer.h"
#include "camp_inventory.h"
#include "character_pane_owned_text.h"
#include "screen_rectangle.h"

namespace owned {
namespace scene_ui {

// A surface as a command names it until S2e defines surface ids: the proxy
// surface id and the generation the command reads. present=false is "none"
// (a null texture), so an id of 0 stays distinct from no surface.
struct SurfaceHandle {
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
    bool present = false;
};
inline bool operator==(const SurfaceHandle& a, const SurfaceHandle& b) noexcept {
    return a.present == b.present && (!a.present || (a.id == b.id && a.generation == b.generation));
}
inline SurfaceHandle surface_handle(const SurfaceImage& image) noexcept {
    return {image.id, image.generation, true};
}

// A character-pane glyph atlas as the overlay cache keys it.
struct GlyphAssetKey {
    std::uint64_t identity = 0, generation = 0, revision = 0;
    bool present = false;
};
inline bool operator==(const GlyphAssetKey& a, const GlyphAssetKey& b) noexcept {
    return a.present == b.present && (!a.present ||
        (a.identity == b.identity && a.generation == b.generation && a.revision == b.revision));
}

// ---- Payloads ----------------------------------------------------------

// One queued UI quad: Renderer::PendingBlit without its resources. A GPU
// source (captured or released surface) is named by home; CPU-converted
// pixels travel in pixels.
struct Quad {
    std::uint64_t source_id = 0, cache_key = 0, generation = 0, base_generation = 0, revision = 0,
        ui_epoch = 0, frame_serial = 0;
    bool native_canvas = true;
    bool output_space = false, filtered_text = false;
    bool keyed = false, gpu = false, cached = false, partial = false, force_opaque_alpha = false,
        batchable_text = false, source_is_surface = false, source_destroyed = false,
        quarter_turn = false, backdrop = false, presentation_fill = false;
    widescreen::HudComposition composition{};
    RECT source{}, destination{}, update{};
    DDCOLORKEY color_key{};
    UINT width = 0, height = 0, pitch = 0;
    std::uint32_t diffuse = 0xffffffffu;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    std::vector<std::uint32_t> pixels;
    // Remote mode (surface protocol): game-format rows the host converts.
    std::vector<std::uint8_t> raw_rows;
    DDPIXELFORMAT raw_format{};
    SurfaceHandle home;
};

// Field-by-field so Quad never depends on Renderer's private PendingBlit
// type: Blit is deduced. Pixels move.
template <class Blit> void quad_values(Blit& from, Quad& to) {
    to.source_id = from.source_id; to.cache_key = from.cache_key; to.generation = from.generation;
    to.base_generation = from.base_generation; to.revision = from.revision;
    to.ui_epoch = from.ui_epoch; to.frame_serial = from.frame_serial;
    to.native_canvas = from.native_canvas; to.output_space = from.output_space;
    to.filtered_text = from.filtered_text; to.keyed = from.keyed; to.gpu = from.gpu;
    to.cached = from.cached; to.partial = from.partial; to.force_opaque_alpha = from.force_opaque_alpha;
    to.batchable_text = from.batchable_text; to.source_is_surface = from.source_is_surface;
    to.source_destroyed = from.source_destroyed; to.quarter_turn = from.quarter_turn;
    to.backdrop = from.backdrop; to.presentation_fill = from.presentation_fill;
    to.composition = from.composition; to.source = from.source; to.destination = from.destination;
    to.update = from.update; to.color_key = from.color_key; to.width = from.width;
    to.height = from.height; to.pitch = from.pitch; to.diffuse = from.diffuse;
    to.u0 = from.u0; to.v0 = from.v0; to.u1 = from.u1; to.v1 = from.v1;
    to.pixels = std::move(from.pixels);
    to.raw_rows = std::move(from.raw_rows); to.raw_format = from.raw_format;
}
template <class Blit> void blit_values(Quad& from, Blit& to) {
    to.source_id = from.source_id; to.cache_key = from.cache_key; to.generation = from.generation;
    to.base_generation = from.base_generation; to.revision = from.revision;
    to.ui_epoch = from.ui_epoch; to.frame_serial = from.frame_serial;
    to.native_canvas = from.native_canvas; to.output_space = from.output_space;
    to.filtered_text = from.filtered_text; to.keyed = from.keyed; to.gpu = from.gpu;
    to.cached = from.cached; to.partial = from.partial; to.force_opaque_alpha = from.force_opaque_alpha;
    to.batchable_text = from.batchable_text; to.source_is_surface = from.source_is_surface;
    to.source_destroyed = from.source_destroyed; to.quarter_turn = from.quarter_turn;
    to.backdrop = from.backdrop; to.presentation_fill = from.presentation_fill;
    to.composition = from.composition; to.source = from.source; to.destination = from.destination;
    to.update = from.update; to.color_key = from.color_key; to.width = from.width;
    to.height = from.height; to.pitch = from.pitch; to.diffuse = from.diffuse;
    to.u0 = from.u0; to.v0 = from.v0; to.u1 = from.u1; to.v1 = from.v1;
    to.pixels = std::move(from.pixels);
    to.raw_rows = std::move(from.raw_rows); to.raw_format = from.raw_format;
}

// Core mode (S3): each payload the host runs declares frame_values (whether
// it travels with the post's frame envelope, as scene_post_frame posts it)
// and run(Renderer&), which resolves its handles through the host Renderer's
// surface table (Renderer::core_residency) and runs the same body as the
// standard-mode closure. Standard mode never calls run().
struct Blits {
    static constexpr bool frame_values = true;
    std::vector<Quad> quads;
    void run(Renderer& renderer);
};

// A full-screen or placed picture (menus, movies, load screens). movie is the
// game's progress movie read at post time for the load reveal.
struct PresentImage {
    static constexpr bool frame_values = true;
    std::vector<Quad> quads;
    widescreen::PresentationFit fit{};
    bool filled = false, fitted = false;
    ProgressMovie movie;
    bool movie_valid = false;
    void run(Renderer& renderer);
};

// Legacy interface draw frame. packets[i].texture is always null in the
// payload; textures[i] names the surface the packet samples.
struct DrawFrame {
    static constexpr bool frame_values = true;
    std::vector<LegacyDrawPacket> packets;
    std::vector<SurfaceHandle> textures;
    std::vector<std::uint8_t> vertex_bytes;
    std::vector<WORD> indices;
    void run(Renderer& renderer);
};

struct Cursor {
    static constexpr bool frame_values = true;
    std::uint64_t cache_key = 0;
    std::vector<std::uint32_t> pixels;
    UINT width = 0, height = 0;
    RECT destination{};
    void run(Renderer& renderer);
};

struct CampDecorationFrame {
    static constexpr bool frame_values = true;
    CampDecoration frame{};
    void run(Renderer& renderer);
};
struct BeginCampPopup {
    static constexpr bool frame_values = true;
    bool clear = false;
    void run(Renderer& renderer);
};
struct EndCampPopup {
    static constexpr bool frame_values = true;
    bool complete = false;
    void run(Renderer& renderer);
};
struct ResetCampPopup {
    static constexpr bool frame_values = false;
    void run(Renderer& renderer);
};

// frame.quads[i].asset is always null in the payload; assets[i] names it.
struct CharacterPaneText {
    static constexpr bool frame_values = true;
    CharacterPaneTextFrame frame;
    std::vector<GlyphAssetKey> assets;
    std::uint64_t ui_epoch = 0, frame_serial = 0;
    RECT destination{};
    bool has_source = false;
    RECT source{};
    // Retained item card text (claude/s3-store): camp-mapped pieces are not
    // interface space, and a card keeps its own composed scale.
    bool interface_space = true, observe_scale = true;
    bool has_fit = false;
    CharacterPaneTextFit fit{};
    void run(Renderer& renderer);  // core mode host
};
// Core mode: one glyph atlas, sent once per (identity, generation, revision)
// before the first text command that names it; the host keeps the latest
// atlas per identity.
struct GlyphAtlas {
    static constexpr bool frame_values = false;
    CharacterPaneGlyphAsset asset;
    void run(Renderer& renderer);
};

// Unstretched capture: compose the world and queued UI, then copy the back
// buffer box into the target surface's GPU home at (x, y).
struct Capture {
    static constexpr bool frame_values = true;
    SurfaceHandle target;
    UINT box_left = 0, box_top = 0, box_right = 0, box_bottom = 0;
    UINT x = 0, y = 0;
    UINT width = 0, height = 0;  // core mode: the target surface's size (the host creates its GPU home)
    void run(Renderer& renderer);  // core mode host
};

struct RetireSurface {
    static constexpr bool frame_values = false;
    std::uint64_t surface_id = 0;
    void run(Renderer& renderer);  // core mode: also drops the host surface table entry
};
struct RetireCampTexture {
    static constexpr bool frame_values = false;
    std::uint64_t surface_id = 0, generation = 0;
    void run(Renderer& renderer);  // core mode host
};
struct FlushUi {
    static constexpr bool frame_values = true;
    void run(Renderer& renderer);
};

struct ScreenRectangle {
    static constexpr bool frame_values = true;
    ScreenRectangleRequest request{};
    D3DVIEWPORT7 viewport{};
    void run(Renderer& renderer);
};

// The HUD border layer at the tooltip boundary (begin_scene_game's frame
// sink): the panel frames copied on the game thread.
struct HudFrameLayer {
    static constexpr bool frame_values = true;
    std::vector<HudAssetFrame> frames;
    void run(Renderer& renderer);
};

// Core mode: one game surface write in the game's own pixel format. whole:
// bytes are the whole surface (pitch x rows); else rows [first_row,
// first_row + rows) at full pitch covering `dirty`. The host keeps a CPU
// mirror per surface id and uploads it through its own Renderer::upload.
struct SurfaceWrite {
    static constexpr bool frame_values = false;
    std::uint64_t id = 0, generation = 0;
    std::uint32_t width = 0, height = 0;
    std::int32_t pitch = 0;
    DDPIXELFORMAT format{};
    bool has_color_key = false;
    DDCOLORKEY color_key{};
    bool static_mip_hint = false, world_mip_use = false, mip_source_used = false;
    bool whole = true;
    RECT dirty{};
    std::uint32_t first_row = 0, rows = 0;
    std::vector<std::uint8_t> bytes;
    void run(Renderer& renderer);
};

// Generic scene packet (legacy figure geometry, HUD figures; proxy
// queueScenePacket). The view's array pointers are null in the payload and
// are bound to the owned arrays when the command runs; cache_identity /
// cache_pointers stay as the opaque borrowed-address cache selector and
// wound_surface_interface as the opaque wound key (never dereferenced).
struct Packet {
    static constexpr bool frame_values = true;  // core mode: the host submits it (run)
    NativeGeometryView view{};
    std::vector<NativePackedGroup> positions, normals;
    std::vector<std::uint32_t> diffuse, specular;
    std::vector<NativeTextureCoordinate> coordinates;
    std::vector<NativeVertexComponent> components;
    std::vector<std::uint16_t> indices;
    std::vector<NativeMorphOffset> morphs;
    std::vector<NativeOwnedTextureMip> mips;   // data pointers null in the payload
    std::vector<std::vector<std::uint8_t>> mip_bytes;
    // Device state at the post (SceneSinks).
    FixedState state{};
    NativeFrameState frame{};
    bool frame_valid = false;
    SurfaceHandle bound, wound;
    // ---- core mode: camp composition and prepared figures (claude/s3-store) ----
    // The game thread's camp placement at the post (SceneSinks applies it
    // while composing); the camp icon surface the view's owned texture names
    // (facts only, no alpha mask); which prepared handles the game view had
    // (bit 0 mesh, 1 material, 2 wound material): the host keeps its own
    // handles per prepared identity (Packet::run).
    camp::SubmissionState camp_state{};
    bool camp_surface_present = false;
    OwnedVisualSurface camp_surface{};
    std::uint8_t prepared_handles = 0;
    // Points the view (and its owned-texture mips) at the arrays above.
    void bind() noexcept {
        if (camp_surface_present) view.owned_texture.surface = &camp_surface;
        view.positions = positions.data(); view.normals = normals.data();
        view.diffuse = diffuse.empty() ? nullptr : diffuse.data();
        view.specular = specular.empty() ? nullptr : specular.data();
        view.texture_coordinates = coordinates.data(); view.components = components.data();
        view.indices = indices.data();
        view.morphs = morphs.empty() ? nullptr : morphs.data();
        for (std::size_t index = 0; index < mips.size(); ++index)
            mips[index].data = mip_bytes[index].empty() ? nullptr : mip_bytes[index].data();
        if (!mips.empty()) view.owned_texture.mips = mips.data();
    }
    void run(Renderer& renderer);
};

// ---- Standard-mode residency (not part of any payload) -----------------
// Keeps what a handle names alive and resolvable until the command ran.
struct Residency {
    struct Home {
        SurfaceHandle handle;
        std::shared_ptr<SurfaceImage> snapshot;           // scene_surface snapshot
        ComPtr<ID3D11Texture2D> texture;                  // a blit's or capture's GPU home
        ComPtr<ID3D11ShaderResourceView> srv;
    };
    std::vector<Home> homes;
    std::vector<std::shared_ptr<const CharacterPaneGlyphAsset>> glyph_assets;

    const Home* find(const SurfaceHandle& handle) const noexcept {
        if (!handle.present) return nullptr;
        for (const auto& home : homes) if (home.handle == handle) return &home;
        return nullptr;
    }
    // One home per handle within a command (the first one answers it; a
    // command's references to one surface generation share one GPU home).
    void add(Home home) {
        if (!home.handle.present || find(home.handle)) return;
        homes.push_back(std::move(home));
    }
    SurfaceImage* image(const SurfaceHandle& handle) const noexcept {
        const Home* home = find(handle);
        return home ? home->snapshot.get() : nullptr;
    }
    std::shared_ptr<SurfaceImage> snapshot(const SurfaceHandle& handle) const {
        const Home* home = find(handle);
        return home ? home->snapshot : nullptr;
    }
    static GlyphAssetKey key_of(const CharacterPaneGlyphAsset* asset) noexcept {
        return asset ? GlyphAssetKey{asset->identity, asset->generation, asset->revision, true} : GlyphAssetKey{};
    }
    void add_glyph_asset(std::shared_ptr<const CharacterPaneGlyphAsset> asset) {
        if (!asset) return;
        const GlyphAssetKey key = key_of(asset.get());
        for (const auto& existing : glyph_assets) if (key_of(existing.get()) == key) return;
        glyph_assets.push_back(std::move(asset));
    }
    std::shared_ptr<const CharacterPaneGlyphAsset> glyph_asset(const GlyphAssetKey& key) const {
        if (!key.present) return nullptr;
        for (const auto& asset : glyph_assets) if (key_of(asset.get()) == key) return asset;
        return nullptr;
    }
};

}  // namespace scene_ui
}  // namespace owned
