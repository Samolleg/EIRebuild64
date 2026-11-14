#pragma once
#include "scene_composite.h"
#include "native_lod_observer.h"
#include "fsr2_upscaler.h"
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
#include "dlss_backend.h"
#include "embedded_d3d12.h"
#endif
#include "fsr2_upscaler_d3d12.h"
#include "bloom.h"
#include "sun_shafts.h"
#include "depth_of_field.h"
#include "camp_layout.h"
#include "gpu_blit_key.h"
#include "embedded_device.h"
#include "embedded_game_effects.h"
#include "embedded_arena.h"
#include "embedded_golem.h"
#include "embedded_curse.h"
#include "frame_plan.h"
#include "d3d12_frame.h"
#include "surface_convert.h"
#include "cave_irradiance.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <atomic>
#include <set>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <functional>
#include <future>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "engine_bridge.h"
#include "world_map_preview.h"
#include "widescreen.h"
#include "shadow_far_retention.h"
#include "debug_overlay.h"
#include "frame_limiter.h"
#include "frame_accounting.h"
#include "frame_statistics.h"
#include "placed_hitch_profile.h"
#include "render_profiler.h"
#include "system_telemetry.h"
#include "weather_wind.h"
#include "wind_trails.h"
#include "ground_cover.h"
#include "snow_trails.h"
#include "point_light_grid.h"
#include "emissive_lights.h"
#include "ambient_terrain_grids.h"
#include "terrain_material.h"
#include "terrain_tile_blend.h"
#include "terrain2_cache.h"
#include "neighbour_fog.h"
#include "shader_cache.h"
#include "scene_thread.h"
#include "test_harness_command.h"
#include "constant_ring.h"

namespace owned {
// Allocation-free attribution, independent of the F10 profiler/recorders.
struct RendererMemoryCounters {
    std::uint64_t geometry = 0, textures = 0, animation = 0, world = 0;
    std::uint64_t surfaceCpu = 0, surfaceGpu = 0, neighbours = 0;
    bool available = false;
};
struct CampDecoration;
struct GcTileGpu;

using Microsoft::WRL::ComPtr;

struct CharacterPaneTextFrame;
struct CharacterPaneTextFit;
struct CharacterPaneGlyphAsset;
struct RetainedSceneIdentitySnapshot;
struct RetainedWorldPointLight;
struct RetainedShadowCasterValue;
struct NativeTerrainShadowEntry;
struct ModelCpuAsset;
struct TerrainTexturePage;
struct SkyFrameGpu;
struct AnimationClipAsset;
struct HarnessCommand;
struct NeighbourMapUpload;
struct NeighbourPiece; // neighbour_piece.h
struct FigureCpuAsset;
struct LiquidSurfField;

struct PlacedHitchArmInputs {
    std::uint64_t previous_bridge_frame = 0;
    std::uint64_t current_world_epoch = 0;
    bool previous_bridge_frame_valid = false;
    bool current_world_epoch_valid = false;
};

PlacedHitchArmInputs placed_hitch_arm_inputs(
    const BridgeProfileSnapshot* previous_profile,
    const RetainedSceneIdentitySnapshot& current_scene) noexcept;
bool arm_placed_hitch_profile(
    PlacedHitchProfile& profile, const PlacedHitchArmInputs& inputs);

struct SurfaceUploadShape {
    bool valid = false;
    UINT width = 0;
    UINT height = 0;
    DWORD flags = 0;
    DWORD fourcc = 0;
    DWORD rgb_bit_count = 0;
    DWORD r_mask = 0;
    DWORD g_mask = 0;
    DWORD b_mask = 0;
    DWORD alpha_mask = 0;
};

// Copy-on-write surface bytes. A completed visual surface shares one
// immutable snapshot with the retained scene (share()) instead of holding a
// second copy; every mutable access detaches first, so a snapshot never
// changes after it is shared.
class SurfacePixels {
public:
    SurfacePixels() = default;
    SurfacePixels(const std::vector<std::uint8_t>& value) : data_(std::make_shared<std::vector<std::uint8_t>>(value)) {}
    SurfacePixels& operator=(const std::vector<std::uint8_t>& value) {
        data_=std::make_shared<std::vector<std::uint8_t>>(value);return *this;
    }
    std::size_t size() const noexcept { return data_?data_->size():0; }
    bool empty() const noexcept { return size()==0; }
    std::size_t capacity() const noexcept { return data_?data_->capacity():0; }
    const std::uint8_t* data() const noexcept { return data_?data_->data():nullptr; }
    std::uint8_t* data() { detach();return data_?data_->data():nullptr; }
    const std::uint8_t& operator[](std::size_t index) const noexcept { return (*data_)[index]; }
    std::uint8_t& operator[](std::size_t index) { detach();return (*data_)[index]; }
    void resize(std::size_t bytes) {
        detach();if(!data_)data_=std::make_shared<std::vector<std::uint8_t>>();data_->resize(bytes);
    }
    void resize(std::size_t bytes,std::uint8_t value) {
        detach();if(!data_)data_=std::make_shared<std::vector<std::uint8_t>>();data_->resize(bytes,value);
    }
    void clear() noexcept { data_.reset(); }
    std::shared_ptr<const std::vector<std::uint8_t>> share() const noexcept { return data_; }
    friend bool operator==(const SurfacePixels& a,const SurfacePixels& b) noexcept {
        return a.size()==b.size() && (a.data_==b.data_ || (a.size() && std::memcmp(a.data(),b.data(),a.size())==0));
    }
    friend bool operator!=(const SurfacePixels& a,const SurfacePixels& b) noexcept { return !(a==b); }
private:
    void detach() { if(data_ && data_.use_count()>1)data_=std::make_shared<std::vector<std::uint8_t>>(*data_); }
    std::shared_ptr<std::vector<std::uint8_t>> data_;
};

// Completed surface writes that produced generations [first, last] and the
// bounds of the pixels they may have changed. `whole` covers the surface.
struct SurfaceChange {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    RECT rect{};
    bool whole = false;
};

struct SurfaceImage {
    std::uint64_t id = 0;
    UINT width = 1;
    UINT height = 1;
    LONG pitch = 4;
    DDPIXELFORMAT format{};
    SurfacePixels pixels;
    // Authored BC mip levels below level zero, tightly packed in 4x4 blocks.
    std::vector<SurfacePixels> bc_mips;
    bool has_color_key = false;
    DDCOLORKEY color_key{};
    std::uint64_t generation = 1;
    // Writes in generation order, so a consumer that saw an older generation
    // can update only what changed since. Written only through
    // advance_surface_generation(); the oldest entries are merged, so old
    // history stays correct but coarse.
    std::vector<SurfaceChange> changes;
    std::uint64_t uploaded_generation = 0;
    SurfaceUploadShape uploaded_shape{};
    RECT dirty_rect{};
    bool dirty_rect_valid = false;
    bool dirty_full = true;
    bool gpu_captured = false;
    bool static_mip_hint = false;
    bool world_mip_use = false;
    bool mip_source_used = false;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D12TextureRef d3d12;  // x64 host, renderer_backend=d3d12: the GPU home instead of texture/srv
    std::uint64_t accounted_cpu_bytes = 0;
    std::uint64_t accounted_gpu_bytes = 0;
    std::uint64_t accounted_double_resident_bytes = 0;
    std::uint64_t accounted_released_bytes = 0;
    // GPU-primary residency (MEMORY_RESIDENCY_POLICY.md). A sampled proxy
    // texture keeps upload-time bytes until this generation has entered draw
    // submission in two distinct frames; known CPU sources stay resident.
    bool gpu_primary = false;  // proxy texture surface, not system memory
    bool cpu_released = false; // pixels empty; texture is the only copy
    bool cpu_access_active = false; // Lock or GDI DC session holds pixels
    bool cpu_pinned = false;   // known CPU reader/composition source: keep resident
    bool visual_source = false;
    std::uint64_t published_generation = 0;
    std::uint64_t sampled_generation = 0;
    std::uint64_t last_sample_frame = 0;
    std::uint8_t sampled_frames = 0;
    std::uint32_t restore_readbacks = 0;
    std::uint64_t last_restore_frame = 0;
    std::uint64_t last_cpu_present = 0; // proxy present serial at the last Lock/DC session
    std::uint64_t texture_serial = 0; // advances when the texture object changes
    // The owned texture cache holds this texture as its copy of a generation
    // (one texture per image). An in-place GPU write must first move the
    // surface to a fresh texture (Renderer::detach_shared_surface_texture),
    // so the cached generation keeps its pixels. In scene mode it is set when
    // the game thread publishes the texture to the scene thread (the cache may
    // share it from then on; scene_detach_surface_texture moves the surface).
    bool texture_shared = false;
    // Scene thread (render_thread option): a copy made on the game thread for
    // one queued command (Renderer::scene_surface). It carries the current
    // GPU home and description but no CPU bytes, is never accounted and is
    // never written back; the game thread's image stays the only owner.
    bool scene_snapshot = false;
};

void update_surface_memory_accounting(SurfaceImage& image) noexcept;
// A pixel-free copy of the surface's GPU home and description (renderer_surfaces.cpp).
std::shared_ptr<SurfaceImage> surface_snapshot(const SurfaceImage& image);
void release_surface_memory_accounting(SurfaceImage& image) noexcept;
// True when converting the CPU pixels to the GPU texture format and packing
// the texture back reproduces every CPU byte (block data, or <=8-bit masks
// that cover the whole pixel and a color key that cannot overwrite alpha).
bool surface_pixels_round_trip_exact(const SurfaceImage& image) noexcept;

struct SurfaceResidencyStats {
    std::uint64_t released_surfaces = 0;
    std::uint64_t released_bytes = 0;
    std::uint64_t restore_readbacks = 0;
    std::uint64_t restore_readback_qpc = 0;
    std::uint64_t pinned_surfaces = 0;
};
SurfaceResidencyStats surface_residency_stats() noexcept;

struct SurfaceUploadResult {
    bool uploaded = false;
    bool recreated = false;
    bool full_surface = false;
    RECT dirty_rect{};
    std::uint64_t upload_bytes = 0;
};

inline SurfaceUploadShape surface_upload_shape(const SurfaceImage& image) noexcept {
    return {true, image.width, image.height, image.format.dwFlags,
            image.format.dwFourCC, image.format.dwRGBBitCount,
            image.format.dwRBitMask, image.format.dwGBitMask,
            image.format.dwBBitMask, image.format.dwRGBAlphaBitMask};
}

inline bool same_surface_upload_shape(const SurfaceUploadShape& left,
                                      const SurfaceUploadShape& right) noexcept {
    return left.valid && right.valid && left.width == right.width &&
        left.height == right.height && left.flags == right.flags &&
        left.fourcc == right.fourcc && left.rgb_bit_count == right.rgb_bit_count &&
        left.r_mask == right.r_mask && left.g_mask == right.g_mask &&
        left.b_mask == right.b_mask && left.alpha_mask == right.alpha_mask;
}

// A draw-only RGB source already has its exact current GPU home. Unkeyed
// output blits can sample it; CPU copies, Lock/DC and keyed conversion restore.
inline bool released_surface_gpu_blit(const SurfaceImage& image,
                                      const DDCOLORKEY* source_key) noexcept {
    return !source_key && image.cpu_released && image.texture && image.srv &&
        !image.dirty_full && !image.dirty_rect_valid &&
        (image.format.dwFlags & DDPF_RGB) && !(image.format.dwFlags & DDPF_FOURCC) &&
        image.uploaded_generation == image.generation &&
        same_surface_upload_shape(image.uploaded_shape,surface_upload_shape(image));
}

inline void clear_surface_dirty(SurfaceImage& image) noexcept {
    image.dirty_rect = {};
    image.dirty_rect_valid = false;
    image.dirty_full = false;
}

inline void mark_surface_dirty_full(SurfaceImage& image) noexcept {
    if(image.mip_source_used)image.static_mip_hint=false;
    image.dirty_rect = {};
    image.dirty_rect_valid = false;
    image.dirty_full = true;
}

inline void mark_surface_dirty(SurfaceImage& image, const RECT& source) noexcept {
    if(image.mip_source_used)image.static_mip_hint=false;
    RECT rect{std::clamp<LONG>(source.left, 0, static_cast<LONG>(image.width)),
              std::clamp<LONG>(source.top, 0, static_cast<LONG>(image.height)),
              std::clamp<LONG>(source.right, 0, static_cast<LONG>(image.width)),
              std::clamp<LONG>(source.bottom, 0, static_cast<LONG>(image.height))};
    if (rect.right <= rect.left || rect.bottom <= rect.top || image.dirty_full) return;
    if (!image.dirty_rect_valid) {
        image.dirty_rect = rect;
        image.dirty_rect_valid = true;
        return;
    }
    image.dirty_rect.left = std::min(image.dirty_rect.left, rect.left);
    image.dirty_rect.top = std::min(image.dirty_rect.top, rect.top);
    image.dirty_rect.right = std::max(image.dirty_rect.right, rect.right);
    image.dirty_rect.bottom = std::max(image.dirty_rect.bottom, rect.bottom);
}

// Precise recent writes kept per surface; older ones merge into one entry.
constexpr std::size_t kSurfaceChangeHistory = 16;

inline void merge_surface_change(SurfaceChange& older, const SurfaceChange& newer) noexcept {
    older.last = newer.last;
    if (older.whole || newer.whole) { older.whole = true; older.rect = {}; return; }
    const bool older_empty = older.rect.right <= older.rect.left || older.rect.bottom <= older.rect.top;
    const bool newer_empty = newer.rect.right <= newer.rect.left || newer.rect.bottom <= newer.rect.top;
    if (older_empty) { older.rect = newer.rect; return; }
    if (newer_empty) return;
    older.rect.left = (std::min)(older.rect.left, newer.rect.left);
    older.rect.top = (std::min)(older.rect.top, newer.rect.top);
    older.rect.right = (std::max)(older.rect.right, newer.rect.right);
    older.rect.bottom = (std::max)(older.rect.bottom, newer.rect.bottom);
}

// Completes one surface write: advances the generation and records the
// changed rectangle (null: the whole surface) for incremental consumers.
inline void advance_surface_generation(SurfaceImage& image, const RECT* changed) noexcept {
    ++image.generation;
    SurfaceChange change{image.generation, image.generation, {}, changed == nullptr};
    if (changed) {
        change.rect = {std::clamp<LONG>(changed->left, 0, static_cast<LONG>(image.width)),
                       std::clamp<LONG>(changed->top, 0, static_cast<LONG>(image.height)),
                       std::clamp<LONG>(changed->right, 0, static_cast<LONG>(image.width)),
                       std::clamp<LONG>(changed->bottom, 0, static_cast<LONG>(image.height))};
        if (change.rect.right <= change.rect.left || change.rect.bottom <= change.rect.top)
            change.rect = {};
    }
    try {
        if (image.changes.size() >= kSurfaceChangeHistory) {
            // Merge only contiguous history; behind an unrecorded write the
            // oldest entry can never be used and is simply dropped.
            if (image.changes[1].first == image.changes[0].last + 1)
                merge_surface_change(image.changes[0], image.changes[1]);
            else
                image.changes[0] = image.changes[1];
            image.changes.erase(image.changes.begin() + 1);
        }
        image.changes.push_back(change);
    } catch (...) {
        image.changes.clear();
    }
}

// Bounds of the pixels changed after generation `since`, up to the current
// one (empty when nothing visible changed). Returns false when that is
// unknown (an unrecorded write, or a whole-surface write): the consumer must
// then update everything.
inline bool surface_changed_since(const SurfaceImage& image, std::uint64_t since,
                                  RECT& region) noexcept {
    region = {};
    if (since == image.generation) return true;
    if (since > image.generation) return false;
    std::uint64_t expected = image.generation;
    SurfaceChange total{};
    for (std::size_t index = image.changes.size(); index-- > 0 && expected > since;) {
        const auto& change = image.changes[index];
        if (change.last != expected || change.first > change.last) return false;
        expected = change.first - 1;
        merge_surface_change(total, change);
    }
    if (expected > since || total.whole) return false;
    region = total.rect;
    return true;
}

enum class OverlayRefresh { reuse, partial, full };

// How much of a cached overlay texture (a converted copy of `source_rect` of
// one surface) must be refreshed for the source's current generation. A
// partial refresh sets `update` to the texture-local rectangle to convert and
// upload; it may be empty when the changes lie outside the cached rectangle.
inline OverlayRefresh overlay_refresh(bool cached_matches, std::uint64_t cached_generation,
                                      const SurfaceImage& source, const RECT& source_rect,
                                      RECT& update) noexcept {
    update = {};
    if (!cached_matches || !cached_generation) return OverlayRefresh::full;
    if (cached_generation == source.generation) return OverlayRefresh::reuse;
    RECT changed{};
    if (!surface_changed_since(source, cached_generation, changed)) return OverlayRefresh::full;
    const RECT local{(std::max)(changed.left, source_rect.left) - source_rect.left,
                     (std::max)(changed.top, source_rect.top) - source_rect.top,
                     (std::min)(changed.right, source_rect.right) - source_rect.left,
                     (std::min)(changed.bottom, source_rect.bottom) - source_rect.top};
    if (local.right > local.left && local.bottom > local.top) update = local;
    return OverlayRefresh::partial;
}

// Device-only creation from completed CPU pixels; safe on an owned loader thread.
HRESULT create_completed_surface_texture(ID3D11Device* device, const SurfaceImage& image,
    ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv);
HRESULT upload_surface_image(ID3D11Device* device, ID3D11DeviceContext* context,
                             SurfaceImage& image,
                             SurfaceUploadResult* result = nullptr);
bool surface_dxgi_format(const DDPIXELFORMAT& format, DXGI_FORMAT& output,
                         UINT& block_bytes) noexcept;
// The texel rows of an upload: the whole surface (full) or its dirty rect,
// converted to the target format (the D3D12 backend's uploads).
HRESULT prepare_surface_upload(SurfaceImage& image, bool full, UINT block_bytes, RECT& rect,
                               ::surface_convert::Prepared& output);
#ifdef EID3D11_TESTING
void test_fail_next_surface_srv_creation() noexcept;
#endif

// Decoded fixed-function vertex, shared by the proxy FVF frontend and the
// embedded homogeneous frontend. W is retained through hardware clipping.
struct FixedFunctionVertex { float x,y,z,w; std::uint32_t diffuse,specular; float u,v; };

struct FixedState {
    std::array<DWORD, 256> render{};
    std::array<std::array<DWORD, 32>, 8> stage{};
    std::array<D3DMATRIX, 4> transform{};
    D3DVIEWPORT7 viewport{};
    D3DMATERIAL7 material{};
    FixedState();
};

#ifdef EID3D11_EMBEDDED
// One native print list, grouped in input order by texture/state like Device's
// proxy print runs. Texture generations stay alive until the list is flushed.
struct EmbeddedPrintBatch {
    struct Run {
        std::shared_ptr<SurfaceImage> texture;
        FixedState state{};
        EmbeddedEffectProjection projection{};
        UINT first = 0, count = 0;
    };
    std::vector<GameParticleGpu> vertices;
    std::vector<Run> runs;
    HRESULT append(const EmbeddedPrintInput&, const FixedState&,
        std::shared_ptr<SurfaceImage>) noexcept;
    void clear() noexcept { vertices.clear(); runs.clear(); }
};
#endif

enum class NativeOpacityClass : std::uint32_t { opaque, cutout, ordered };
struct NativeAlphaEvidence {
    bool texture_opaque = false;
    float diffuse_alpha = std::numeric_limits<float>::quiet_NaN();
    bool ordered_blend = false;
};
bool native_texture_alpha_opaque(const NativeOwnedTextureView& texture,
                                std::uint32_t mip_filter) noexcept;
NativeOpacityClass native_opacity_class(const FixedState& state,
    float material_alpha, NativeAlphaEvidence alpha = {}) noexcept;
bool native_alpha_test_passes_all(const FixedState& state, NativeAlphaEvidence alpha) noexcept;
NativeOwnedFixedState native_owned_fixed_state(const FixedState& state) noexcept;
// Fixed-function state of one render-only neighbour-map record, derived from
// the current frame state.
FixedState neighbour_submission_state(const FixedState& frame,SemanticKind kind,
    NativeOrdering ordering) noexcept;

// Owned immutable-tree packets inherit only authoritative frame-global state.
// Every tree-local material/render field is supplied by its owned contract.
FixedState native_owned_submission_state(
    const FixedState& frame_state,
    const NativeOwnedFixedState& owned_state) noexcept;

struct LegacyDrawPacket {
    D3DPRIMITIVETYPE type = D3DPT_TRIANGLELIST;
    DWORD fvf = 0;
    DWORD vertex_count = 0;
    std::size_t vertex_offset = 0;
    DWORD index_count = 0;
    std::size_t index_offset = 0;
    FixedState state{};
    SurfaceImage* texture = nullptr;
    SemanticKind semantic_kind = SemanticKind::unknown;
    std::uint32_t caller_rva = 0;
    std::uint32_t legacy_draw_count = 1;
    widescreen::HudComposition hud_composition{};
    camp::Composition camp_placement{};
    bool camp_placement_active = false;
};

UINT legacy_fvf_stride(DWORD fvf);

POINT capture_stretch_source_point(const RECT& source, const RECT& destination,
                                   LONG destination_x,
                                   LONG destination_y) noexcept;

struct LegacyClearScope {
    bool valid = false;
    bool full_surface = false;
};

inline LegacyClearScope legacy_clear_scope(
    DWORD rectangle_count, const D3DRECT* rectangles) noexcept {
    return {rectangle_count == 0 || rectangles != nullptr, rectangle_count == 0};
}

inline bool normalize_legacy_clear_rectangle(
    const D3DRECT& source, UINT width, UINT height,
    D3D11_RECT& destination) noexcept {
    destination.left = std::clamp<LONG>(source.x1, 0, static_cast<LONG>(width));
    destination.top = std::clamp<LONG>(source.y1, 0, static_cast<LONG>(height));
    destination.right = std::clamp<LONG>(source.x2, 0, static_cast<LONG>(width));
    destination.bottom = std::clamp<LONG>(source.y2, 0, static_cast<LONG>(height));
    return destination.right > destination.left &&
        destination.bottom > destination.top;
}

inline UINT native_specular_addition_enabled(DWORD render_state) noexcept {
    return render_state != FALSE ? 1u : 0u;
}

inline void copy_direct_bgra_rows(const std::uint8_t* source,
                                  std::size_t source_pitch,
                                  std::uint32_t* destination,
                                  UINT width, UINT height) noexcept {
    const std::size_t row_bytes = static_cast<std::size_t>(width) *
        sizeof(std::uint32_t);
    for (UINT y = 0; y < height; ++y)
        std::memcpy(destination + static_cast<std::size_t>(y) * width,
                    source + static_cast<std::size_t>(y) * source_pitch,
                    row_bytes);
}

inline DWORD ui_blit_alpha_operation(bool force_opaque_alpha) noexcept {
    return force_opaque_alpha ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
}

// Reproduce the legacy renderer's immediate selection-metadata contract from
// the same object-space packet consumed by the GPU. This is game interaction
// metadata, not a second rendering path: no lit/transformed vertex stream is
// retained or submitted by the CPU.
HRESULT calculate_native_screen_extents(const NativeGeometryView& view,
                                        const D3DVIEWPORT7& viewport,
                                        RECT& extents) noexcept;
HRESULT calculate_native_aabb_screen_extents(const float* minimum, const float* maximum,
    const std::array<float,16>& transform, const D3DVIEWPORT7& viewport, RECT& extents) noexcept;

struct NativeMeshContentKey {
    std::uint64_t fingerprint = 0;
    bool valid = false;
};

inline std::uint64_t native_mesh_content_hash_bytes(
    std::uint64_t hash, const void* bytes, std::size_t size) noexcept {
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

inline NativeMeshContentKey native_mesh_content_key(
    const NativeGeometryView& view) noexcept {
    if (view.decode || !view.positions || !view.normals || !view.components ||
        !view.texture_coordinates || !view.indices || !view.position_groups ||
        !view.normal_groups || !view.component_count ||
        !view.texture_coordinate_count || !view.index_count)
        return {};

    const std::uint64_t position_bytes =
        static_cast<std::uint64_t>(view.position_groups) * sizeof(NativePackedGroup);
    const std::uint64_t normal_bytes =
        static_cast<std::uint64_t>(view.normal_groups) * sizeof(NativePackedGroup);
    const std::uint64_t component_bytes =
        static_cast<std::uint64_t>(view.component_count) * sizeof(NativeVertexComponent);
    const std::uint64_t texture_bytes =
        static_cast<std::uint64_t>(view.texture_coordinate_count) *
        sizeof(NativeTextureCoordinate);
    const std::uint64_t index_bytes =
        static_cast<std::uint64_t>(view.index_count) * sizeof(std::uint16_t);
    const std::uint64_t color_bytes =
        static_cast<std::uint64_t>(view.normal_groups) * 4u *
        sizeof(std::uint32_t);
    const bool has_terrain =
        (view.decoded_channels & native_decoded_terrain) != 0;
    const std::uint64_t terrain_bytes = has_terrain
        ? static_cast<std::uint64_t>(view.terrain_vertex_count) *
            sizeof(NativeTerrainVertex)
        : 0;
    constexpr auto maximum_size =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if (position_bytes > maximum_size || normal_bytes > maximum_size ||
        component_bytes > maximum_size || texture_bytes > maximum_size ||
        index_bytes > maximum_size || color_bytes > maximum_size ||
        terrain_bytes > maximum_size ||
        (has_terrain && (!view.terrain_vertices ||
                         view.terrain_vertex_count != view.component_count)))
        return {};

    const std::uint32_t color_presence =
        (view.diffuse ? 1u : 0u) | (view.specular ? 2u : 0u);
    const std::array<std::uint32_t, 8> description{
        view.position_groups, view.normal_groups, view.component_count,
        view.texture_coordinate_count, view.index_count, view.decoded_channels,
        color_presence, has_terrain ? view.terrain_vertex_count : 0u,
    };
    std::uint64_t hash = native_mesh_content_hash_bytes(
        1469598103934665603ull, description.data(), sizeof(description));
    hash = native_mesh_content_hash_bytes(
        hash, view.positions, static_cast<std::size_t>(position_bytes));
    hash = native_mesh_content_hash_bytes(
        hash, view.normals, static_cast<std::size_t>(normal_bytes));
    hash = native_mesh_content_hash_bytes(
        hash, view.components, static_cast<std::size_t>(component_bytes));
    hash = native_mesh_content_hash_bytes(
        hash, view.texture_coordinates, static_cast<std::size_t>(texture_bytes));
    hash = native_mesh_content_hash_bytes(
        hash, view.indices, static_cast<std::size_t>(index_bytes));
    if (view.diffuse)
        hash = native_mesh_content_hash_bytes(
            hash, view.diffuse, static_cast<std::size_t>(color_bytes));
    if (view.specular)
        hash = native_mesh_content_hash_bytes(
            hash, view.specular, static_cast<std::size_t>(color_bytes));
    if (has_terrain)
        hash = native_mesh_content_hash_bytes(
            hash, view.terrain_vertices, static_cast<std::size_t>(terrain_bytes));
    return {hash, true};
}

struct NativeMeshCacheSelector {
    std::uintptr_t cache_identity = 0;
    std::uintptr_t object_token = 0;
    std::uintptr_t figure_token = 0;
    std::uintptr_t geometry_token = 0;
    std::uintptr_t positions = 0;
    std::uintptr_t normals = 0;
    std::uintptr_t components = 0;
    std::uintptr_t texture_coordinates = 0;
    std::uintptr_t indices = 0;
    std::uint64_t content_fingerprint = 0;
    bool content_keyed = false;

    bool operator==(const NativeMeshCacheSelector& other) const noexcept {
        return cache_identity == other.cache_identity &&
            object_token == other.object_token &&
            figure_token == other.figure_token &&
            geometry_token == other.geometry_token &&
            positions == other.positions && normals == other.normals &&
            components == other.components &&
            texture_coordinates == other.texture_coordinates &&
            indices == other.indices &&
            content_fingerprint == other.content_fingerprint &&
            content_keyed == other.content_keyed;
    }
};

inline NativeMeshCacheSelector native_mesh_cache_selector(
    const NativeGeometryView& view,
    const NativeMeshContentKey& content) noexcept {
    NativeMeshCacheSelector selector{};
    if (!view.geometry_revision_valid) {
        // Placed/interface geometry has no verified source revision. Its
        // immutable bytes are therefore the only authoritative cache key;
        // borrowed pointers and opaque object tokens may be reused in place.
        selector.content_keyed = true;
        selector.content_fingerprint = content.fingerprint;
        return selector;
    }
    selector.cache_identity = view.cache_identity;
    selector.object_token = view.object_token;
    selector.figure_token = view.figure_token;
    selector.geometry_token = view.geometry_token;
    if (!selector.cache_identity && view.cache_pointers_valid) {
        selector.positions = view.cache_pointers[0];
        selector.normals = view.cache_pointers[1];
        selector.components = view.cache_pointers[2];
        selector.texture_coordinates = view.cache_pointers[3];
        selector.indices = view.cache_pointers[4];
    } else if (!selector.cache_identity) {
        selector.positions = reinterpret_cast<std::uintptr_t>(view.positions);
        selector.normals = reinterpret_cast<std::uintptr_t>(view.normals);
        selector.components = reinterpret_cast<std::uintptr_t>(view.components);
        selector.texture_coordinates =
            reinterpret_cast<std::uintptr_t>(view.texture_coordinates);
        selector.indices = reinterpret_cast<std::uintptr_t>(view.indices);
    }
    return selector;
}

inline bool native_mesh_revision_requires_refresh(
    bool found, bool cached_revision_valid,
    const std::array<std::uint32_t, 5>& cached_revision,
    const NativeGeometryView& view) noexcept {
    return found && view.geometry_revision_valid &&
        (!cached_revision_valid || cached_revision != view.geometry_revision);
}

inline bool native_mesh_color_revision_requires_refresh(
    bool found, bool topology_refresh,
    bool cached_revision_valid,
    const std::array<std::uint32_t, 3>& cached_revision,
    const NativeGeometryView& view) noexcept {
    return found && !topology_refresh && view.color_revision_valid &&
        (!cached_revision_valid || cached_revision != view.color_revision);
}

struct DebugResourceViewWorkSnapshot {
    std::uint64_t passes = 0;
    std::uint64_t draws = 0;
    std::uint64_t resource_copies = 0;
    std::uint64_t readbacks = 0;
    std::uint64_t target_allocations = 0;
    std::uint64_t pipeline_creations = 0;
};

// Shadow pass constant buffer shared by directional and point shadow rendering.
struct NativeShadowConstants {
    std::array<std::array<float,16>,6> light{}, camera{};
    std::array<std::array<float,4>,6> bias{};
    std::array<UINT,4> control{};
    // Static sun caster bake on a scrolled map (shadowScrollCopied/Control):
    // the copied old square in light NDC (x min, x max, y min, y max) and
    // x = 1 when the build culls records wholly inside it, y = map side.
    std::array<float,4> scroll_copied{};
    std::array<UINT,4> scroll_control{};
};
static_assert(sizeof(NativeShadowConstants)==912, "owned shadow constant layout");

inline bool configured_boolean(const RendererConfig& config,const char* key,bool fallback){const auto* option=config.find(key);return option&&option->type==RendererOptionType::boolean?option->boolean_value:fallback;}
inline int configured_integer(const RendererConfig& config,const char* key,int fallback){const auto* option=config.find(key);return option&&option->type==RendererOptionType::integer?option->integer_value:fallback;}
// The terrain material switches after the terrain_quality tier (terrain_material.h).
inline TerrainMaterialSwitches configured_terrain_material(const RendererConfig& config){
 return terrain_material_switches(configured_integer(config,"terrain_quality",2),
  configured_boolean(config,"terrain_antitile",true),configured_boolean(config,"terrain_height_blend",true));
}
// The generated native world shader source (renderer.cpp).
const std::string& owned_native_shader_source();
const std::string& owned_ambient_particle_shader_source();
const std::string& owned_ground_cover_shader_source();
// Each feature's shader programs (shader_cache.h) in the order its pipeline
// compiles them. Renderer::prepare_shaders prepares the ones the current
// settings use, so the features find them in the shader cache.
void append_interface_shader_programs(std::vector<ShaderProgram>& out);
void append_world_support_shader_programs(std::vector<ShaderProgram>& out);
// Named programs of the interface and world-support lists. Consumers ask for
// a program by name; the enum order is only the list (preparation) order, so
// inserting a program never shifts what another consumer compiles.
enum class InterfaceShader : std::size_t {
 game_vs,game_ps,rectangle_vs,rectangle_ps,backdrop_ps,count
};
enum class WorldSupportShader : std::size_t {
 clear_vs,clear_ps,game_particle_vs,game_particle_ps,shadow_scroll_vs,shadow_scroll_ps,
 interface_scaled_ps,game_particle_depth_ps,game_ribbon_vs,game_selection_vs,game_mesh_vs,game_glyph_vs,interface_text_ps,count
};
ShaderProgram interface_shader_program(InterfaceShader shader);
ShaderProgram world_support_shader_program(WorldSupportShader shader);
void append_native_shader_programs(std::vector<ShaderProgram>& out);
void append_native_off_material_programs(std::vector<ShaderProgram>& out);
void append_terrain_tessellation_shader_programs(std::vector<ShaderProgram>& out);
void append_terrain2_cache_shader_programs(std::vector<ShaderProgram>& out); // the material cache bake (terrain2_cache.h)
void append_terrain2_cache_d3d12_programs(std::vector<ShaderProgram>& out); // the cache's compute bake (DXIL table only)
void append_impostor_shader_programs(std::vector<ShaderProgram>& out);
void append_hiz_shader_programs(std::vector<ShaderProgram>& out);
void append_shadow_range_shader_programs(std::vector<ShaderProgram>& out);
void append_liquid_wave_shader_programs(std::vector<ShaderProgram>& out);
void append_ambient_occlusion_shader_programs(std::vector<ShaderProgram>& out);
void append_sky_shader_programs(std::vector<ShaderProgram>& out);
// The x64 host's DXIL-only programs (renderer_native_d3d12.cpp): GenerateMips
// and the gamma ramp's (renderer_frame.cpp gammaVS, gammaPS) on D3D12.
void append_d3d12_shader_programs(std::vector<ShaderProgram>& out);
void append_debug_resource_shader_programs(std::vector<ShaderProgram>& out);
void append_gamma_shader_programs(std::vector<ShaderProgram>& out);
const ShaderProgram& d3d12_mip_program() noexcept;
const ShaderProgram& d3d12_mip_array_program() noexcept;
const ShaderProgram& d3d12_mip_array_range_program() noexcept; // every slice of the views, one dispatch z a slice
void append_fog_volume_shader_programs(std::vector<ShaderProgram>& out);
void append_ambient_particle_shader_programs(std::vector<ShaderProgram>& out);
void append_wind_trail_shader_programs(std::vector<ShaderProgram>& out);
void append_precipitation_shader_programs(std::vector<ShaderProgram>& out);
void append_ground_cover_shader_programs(std::vector<ShaderProgram>& out);
void append_critter_shader_programs(std::vector<ShaderProgram>& out);
void append_fur_shader_programs(std::vector<ShaderProgram>& out);
void append_cave_irradiance_shader_programs(std::vector<ShaderProgram>& out);
void append_terrain2_shader_programs(std::vector<ShaderProgram>& out); // the height bake (terrain2.h)
void append_snow_surface_program(std::vector<ShaderProgram>& out); // the snow surface copy (terrain2.h kSnowSurfaceShader)
void append_terrain2_cover_programs(std::vector<ShaderProgram>& out); // ground cover's 2.0 classes (terrain2_cover.h)
// The program groups the settings use, one bit per group (feature level 11
// adds every world group), and the programs of the groups in a mask.
std::uint32_t shader_groups_for_settings(const RendererConfig& config,bool feature_level_11);
void append_shader_groups(std::uint32_t groups,std::vector<ShaderProgram>& out);
// Settings-specialised native pixel programs (renderer.cpp; native_shader.h
// NATIVE_OFF_*): the 25 pixel programs of a key with its defines (false: the
// list does not match the specialised table), the shipped presets' keys and
// their point-shadow siblings (the build precompiles these into the disk
// cache), and a program's cache file name (eid3d11_shader_cache next to
// the DLL).
struct NativeSpecialisedPrograms{std::vector<ShaderProgram> programs;std::vector<std::vector<D3D_SHADER_MACRO>> macros;};
bool native_specialised_programs(std::uint32_t key,NativeSpecialisedPrograms& out);
std::vector<std::uint32_t> native_shipped_specialisation_keys();
std::wstring native_spec_cache_file_name(const ShaderKey& key);
// A terrain's tile families (terrain_tile_blend.h) loaded off the render thread:
// its map editor profile, else the editor preset. Invalid without resource mounts.
// Dropping an unfinished job never waits for it: the job is cancelled and its
// future handed to a reaper (reap_tile_family_jobs, each frame), joined at
// teardown (join_tile_family_jobs). A world change released neighbour cover
// whose std::async future then blocked the game thread for 27 s on a starved
// below-normal job.
class TileFamiliesJob {
public:
    TileFamiliesJob() = default;
    TileFamiliesJob(std::future<TerrainTileFamilies> result,std::shared_ptr<std::atomic<bool>> cancel) noexcept
        : result_(std::move(result)),cancel_(std::move(cancel)) {}
    TileFamiliesJob(TileFamiliesJob&&) noexcept = default;
    TileFamiliesJob& operator=(TileFamiliesJob&& other) noexcept {
        if(this!=&other){retire();result_=std::move(other.result_);cancel_=std::move(other.cancel_);}
        return *this;
    }
    TileFamiliesJob(const TileFamiliesJob&) = delete;
    TileFamiliesJob& operator=(const TileFamiliesJob&) = delete;
    ~TileFamiliesJob(){retire();}
    bool valid() const noexcept {return result_.valid();}
    bool ready() const {return result_.valid()&&result_.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
    TerrainTileFamilies get(){cancel_.reset();return result_.get();}
private:
    void retire() noexcept;
    std::future<TerrainTileFamilies> result_;
    std::shared_ptr<std::atomic<bool>> cancel_;
};
TileFamiliesJob load_terrain_tile_families_async(const TerrainCpuAsset& terrain);
void reap_tile_family_jobs() noexcept;
void join_tile_family_jobs() noexcept;
struct TerrainTileBlendInput; // owned_terrain_decode.h
inline float configured_floating_point(const RendererConfig& config,const char* key,float fallback){const auto* option=config.find(key);return option&&option->type==RendererOptionType::floating_point?option->floating_value:fallback;}
// The Low pixel path (NATIVE_LOW shader variants): every stage it leaves out
// is off under these settings (no soft or point shadows, no ground contact
// blend, tile blend, cliffs or terrain material stage, no water reflections
// and so no physical water), so only the sun shadow changes: four taps
// instead of the 25-tap footprint, read from the only maps Low draws (two or
// one close layers, no far layers) with one narrower blend between them.
// Terrain cliffs run only with the terrain material blend on (user decision
// 2026-10-10: the side projection depends on the blend's family ownership;
// blend 0 draws faces exactly as terrain_cliffs 0). The stored terrain_cliffs
// value is kept, so cliffs return with the blend.
inline bool configured_terrain_cliffs(const RendererConfig& config){
 return configured_boolean(config,"terrain_cliffs",true)&&configured_integer(config,"terrain_blend",1)>0;
}
inline bool native_low_pixel_path(const RendererConfig& config){
 const TerrainMaterialSwitches material=configured_terrain_material(config);
 return configured_integer(config,"shadow_softness",1)<=0&&!configured_boolean(config,"point_shadows",true)&&
  configured_floating_point(config,"contact_blend",.7f)<=0&&configured_integer(config,"terrain_blend",1)<=0&&
  !configured_terrain_cliffs(config)&&!material.antitile&&!material.height_blend&&
  !configured_boolean(config,"water_reflections",true)&&
  configured_integer(config,"shadow_close_cascades",3)<=2&&!configured_boolean(config,"shadow_far",true);
}
// Target formats that a feature's targets and the load hold's pipeline
// warm-up (renderer_warmup.cpp) share: the bloom glow, the particle depth
// for reflections, the half-resolution water reflection targets and depth,
// and the point-shadow atlas depth (atlas, overlay).
constexpr DXGI_FORMAT kGlowFormat=DXGI_FORMAT_R11G11B10_FLOAT;
constexpr DXGI_FORMAT kParticleDepthFormat=DXGI_FORMAT_R32_FLOAT;
constexpr DXGI_FORMAT kWaterSsrFormats[6]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,
 DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};
constexpr DXGI_FORMAT kWaterSsrDepthFormat=DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT kPointShadowDepthFormats[2]={DXGI_FORMAT_D16_UNORM,DXGI_FORMAT_D32_FLOAT};

// Storage of one buffer or texture from its description (renderer.cpp).
std::uint64_t resource_video_bytes(ID3D11Resource* resource) noexcept;

// Scene thread (render_thread option): what a queued present needs from the
// game thread, copied when the frame is queued. Harness steps that act on the
// game (load, quit, camera, pause) ran on the game thread already; only the
// renderer verb it returned travels here.
struct ScenePresentInputs {
    bool harness_command_valid = false;
    HarnessCommand harness_command;
    unsigned long harness_entry_id = 0;
    unsigned harness_camera_steps = 0;
    bool harness_on_travel_map = false;
    std::string harness_banner;  // note verb: the route banner the overlay shows
    BridgeFrameValues bridge{};
    BridgeProfileValues profile{};
    std::int64_t recorder_present_entry = 0;
    bool outer_timing_taken = false;
    OuterFrameTimingSnapshot outer_timing{};
    frame_acct::Record game_frame{}; // the game thread's frame ending at this present
    std::vector<HudAssetFrame> hud_frames; // live HUD panel reads, copied on the game thread
};

// First statement of a public Renderer entry point that touches renderer
// state: a game-thread call while the scene thread owns the renderer first
// waits for it (scene_sync), and the whole call then runs inline: a nested
// public call inside it (flush_native from flush_native_static_terrain, ...)
// must not be queued, or the scene thread would run it on the immediate
// context while this thread is still using it. No effect with the
// render_thread option off or on the scene thread itself. On the game thread
// the entry is also charged to the ours_renderer frame-accounting part
// (frame_accounting.h; harness runs only).
#define SCENE_ENTRY(reason) const ::frame_acct::Scope frame_acct_entry_(this->on_scene_thread() ? ::frame_acct::none : ::frame_acct::entry_part()); const ::owned::Renderer::SceneInlineScope scene_inline_scope_(const_cast<Renderer*>(this),reason)

} // namespace owned
#include "scene_commands_world.h"
namespace owned {

namespace scene_core { struct Present; struct Config; struct Resize; struct Readback; struct CaptureRequest; struct ProgressFrames; struct DisplayApplyResult; }  // core-mode commands (scene_core_types.h)
namespace scene_ui {  // UI payloads the core-mode host runs (scene_commands_ui.h)
struct Quad; struct Residency; struct SurfaceHandle; struct Blits; struct PresentImage; struct DrawFrame; struct Cursor;
struct RetireSurface; struct FlushUi; struct ScreenRectangle; struct HudFrameLayer; struct SurfaceWrite; struct Packet;
struct Capture; struct CampDecorationFrame; struct BeginCampPopup; struct EndCampPopup; struct ResetCampPopup;
struct CharacterPaneText; struct GlyphAtlas;
}

class MaterialLibrary;

class Renderer {
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
    // Declared first: the local executor outlives all renderer GPU members.
    // Startup binds frame() only after configuration selects D3D12.
    EmbeddedD3D12 embedded_d3d12_;
    bool embedded_d3d12_presented_ = false;
    bool embedded_d3d12_failed_ = false;
    // Prepared startup; connect only with the source CDX11 device adapters.
    HRESULT ensure_embedded_d3d12();
    HRESULT fail_embedded_d3d12(const char* operation, HRESULT result, const char* reason = nullptr) noexcept;
#endif
    friend struct ContactBlendSmokeAccess;
    friend struct LiquidSurfaceSmokeAccess;
    friend struct SceneCompositeSmokeAccess;
    friend struct AmbientMapRegressionAccess;
    // Typed scene commands (scene_commands_world.h) run Renderer internals.
    friend struct scene_cmd::BeginEngineFrame;
    friend struct scene_cmd::HitTestFrame;
    friend struct scene_cmd::BeginScene;
    friend struct scene_cmd::EndScene;
    friend struct scene_cmd::FlushNative;
    friend struct scene_cmd::CompositeWorld;
    friend struct scene_cmd::WorldLighting;
    friend struct scene_cmd::Clear;
    friend struct scene_cmd::GameParticles;
    friend struct scene_cmd::GameRibbon;
    friend struct scene_cmd::GameSelection;
    friend struct scene_cmd::GameGlyphs;
    friend struct scene_cmd::GamePrintRun;
    friend struct scene_cmd::DiscardNative;
    friend struct scene_cmd::TakeNativeCheckpoint;
    friend struct scene_cmd::ReleaseNativeCheckpoint;
    friend struct scene_cmd::RollbackNative;
    friend struct scene_cmd::AbortDirectionalShadows;
    friend struct scene_cmd::AbortPointShadowFaces;
    friend struct scene_core::Present;
    friend struct scene_core::Config;
    friend struct scene_core::Resize;
    friend struct scene_core::Readback;
    friend struct scene_core::CaptureRequest;
    friend struct scene_core::ProgressFrames;
    friend struct scene_core::DisplayApplyResult;
    friend struct scene_ui::Blits;
    friend struct scene_ui::PresentImage;
    friend struct scene_ui::DrawFrame;
    friend struct scene_ui::Cursor;
    friend struct scene_ui::RetireSurface;
    friend struct scene_ui::FlushUi;
    friend struct scene_ui::ScreenRectangle;
    friend struct scene_ui::HudFrameLayer;
    friend struct scene_ui::SurfaceWrite;
    friend struct scene_ui::Packet;
    friend struct scene_ui::Capture;
    friend struct scene_ui::CampDecorationFrame;
    friend struct scene_ui::BeginCampPopup;
    friend struct scene_ui::EndCampPopup;
    friend struct scene_ui::ResetCampPopup;
    friend struct scene_ui::CharacterPaneText;
    friend struct scene_ui::GlyphAtlas;
    friend struct scene_cmd::MarkLoadPicture;
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();
    struct NativeQueueCheckpoint {
        std::size_t draws = 0;
        std::size_t dynamic_bytes = 0;
        std::uint64_t epoch = 0;
        std::uint64_t scene_token = 0; // scene mode: the scene thread holds the real checkpoint
    };
    HRESULT ensure(HWND hwnd, UINT width, UINT height);
    // Explicit test entry point for headless WARP lifecycle checks. Production
    // initialization continues to use ensure(), which selects hardware.
    HRESULT ensure_for_test_warp(HWND hwnd, UINT width, UINT height);
    using TestInitializationObserver = void (*)(void*, const char*) noexcept;
    void set_test_initialization_observer(TestInitializationObserver observer,
        void* context) noexcept {
        test_initialization_observer_ = observer;
        test_initialization_observer_context_ = context;
    }
    const char* test_initialization_failure_stage() const noexcept {
        return test_initialization_failure_stage_;
    }
    HRESULT ensure_with_driver(HWND hwnd, UINT width, UINT height, D3D_DRIVER_TYPE driver);
    HRESULT resize(UINT width, UINT height);
    // Core mode: the Apply comes from the host overlay (Control) and its result goes back.
    bool consume_display_apply(DisplaySettings& settings) noexcept {
        return core_mode_ ? core_take_display_apply(settings) : debug_overlay_.consume_display_apply(settings);
    }
    void finish_display_apply(const DisplaySettings& settings, HRESULT result, const char* message) noexcept {
        if (core_mode_) core_display_apply_result(settings, result, message);
        else debug_overlay_.finish_display_apply(settings, result, message);
    }
    HRESULT clear(DWORD rectangle_count, const D3DRECT* rectangles,
                  DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
        return clear(0, nullptr, flags, color, z, stencil);
    }
    // S_FALSE borrows active rendering (or selects D3D11). S_OK gives
    // the caller ownership of the corresponding finish boundary.
    HRESULT begin_embedded_simulation();
    void finish_embedded_frame();
    HRESULT begin_engine_frame();
    HRESULT retire_embedded_world();
    RendererMemoryCounters read_memory_counters();
    Microsoft::WRL::ComPtr<IDXGIAdapter> graphics_adapter() noexcept;
    // The global map's zone hit-test frame (#200; bridge hook on the game's
    // GenerateHitTestDDS call): begin redirects the world target to the
    // lossless hit-test target, end restores it.
    void hit_test_frame(bool begin);
    void begin_scene();
    void end_scene();
    HRESULT draw(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                 DWORD vertex_count, const WORD* indices, DWORD index_count,
                 const FixedState& state, SurfaceImage* texture,
                 SemanticKind semantic_kind = SemanticKind::unknown,
                 std::uint32_t caller_rva = 0,
                 std::uint32_t legacy_draw_count = 1,
                 const widescreen::HudComposition& hud_composition = {},
                 ID3D11ShaderResourceView* owned_texture = nullptr);
#ifdef EID3D11_EMBEDDED
    // Synchronous bounded source values; never retains source pointers.
    HRESULT draw_embedded_fixed_function(D3DPRIMITIVETYPE type, const FixedFunctionVertex* vertices,
        DWORD vertex_count, const WORD* indices, DWORD index_count,
        const FixedState& state, ID3D11ShaderResourceView* texture, SurfaceImage* surface = nullptr);
#endif
    HRESULT draw_frame(const std::vector<LegacyDrawPacket>& packets,
                       const std::vector<std::uint8_t>& vertex_bytes,
                       const std::vector<WORD>& indices);
    // The device's pending legacy draws; in scene mode the vectors move into
    // the queued command (left empty) with snapshots of their textures.
    HRESULT draw_frame_take(std::vector<LegacyDrawPacket>& packets,
                            std::vector<std::uint8_t>& vertex_bytes,
                            std::vector<WORD>& indices);
    HRESULT queue_native(const NativeGeometryView& view, const FixedState& state,
                         SurfaceImage* texture,
                         NativeGeometryResult* result = nullptr);
#ifdef EID3D11_EMBEDDED
    HRESULT queue_embedded_records(const NativePreparedSubmission& submission,
        const FixedState& state, std::uint64_t primary,
        const OwnedVisualSurface* secondary, NativeGeometryResult* result);
    HRESULT queue_embedded_figure(const NativeGeometryView& view, const FixedState& state,
        ID3D11ShaderResourceView* primary, ID3D11ShaderResourceView* secondary,
        const NativeWorldLighting* lighting, const std::vector<RetainedWorldPointLight>& lights,
        const OwnedVisualSurface* primarySurface = nullptr,
        const OwnedVisualSurface* secondarySurface = nullptr);
    HRESULT queue_embedded_wound(const NativeGeometryView& view, const FixedState& state,
        const OwnedVisualSurface& surface, NativeGeometryResult* result);
#endif
    HRESULT queue_native_direct(const NativeGeometryView& view, const FixedState& state,
                                 ID3D11ShaderResourceView* texture,
                                 NativeGeometryResult* result = nullptr);
    HRESULT queue_native_prepared(const NativeGeometryView& view,
                                  const FixedState& state,
                                  SurfaceImage* texture,
                                  NativeGeometryResult* result = nullptr);
    HRESULT queue_native_owned(const NativeGeometryView& view,
                               const FixedState& state,
                               NativeGeometryResult* result = nullptr);
    HRESULT queue_native_depth(const NativeDepthSubmission& submission,const FixedState& frame,NativeGeometryResult* result);
    // casters: the bridge's whole static store at store_revision (nullptr:
    // unchanged since the last call). On a static-layer redraw the bake is
    // drawn and the absent casters a redrawn map reaches are returned in
    // missing for registration.
    bool queue_native_static_shadow_depth(const RetainedShadowCasterValue* casters,
        std::uint32_t count,std::uint64_t store_revision,
        std::vector<RetainedShadowCasterValue>& missing) noexcept;
    // Terrain sun casters of a static-layer redraw with their final masks;
    // absent: entries the bridge must submit through the depth path.
    bool queue_native_terrain_shadow_bake(const NativeTerrainShadowEntry* entries,
        std::uint32_t count,std::vector<std::uint32_t>& absent) noexcept;
    HRESULT queue_native_records(const NativePreparedSubmission& submission,
                                 const FixedState& frame_state,SurfaceImage* wound,
                                 NativeGeometryResult* result = nullptr);
    HRESULT flush_native(unsigned layout_slot = 0);
    // The interface depth inside a native-canvas rectangle back to 1 (an owned
    // portrait's frame: its depth stays its own; scene_core::Preview::run).
    void clear_interface_depth(float left, float top, float right, float bottom) noexcept;
    // Persistent static color sets (see NativeStaticSubmission): static
    // objects, and base terrain (drawn first, before the occlusion pyramid).
    HRESULT set_native_static_object(std::uint64_t key,const NativePreparedSubmission& submission,
                                     const FixedState& frame_state,bool& registered,
                                     NativeGeometryResult* result = nullptr);
    HRESULT set_native_static_terrain(std::uint64_t key,const NativeGeometryView& view,
                                      const FixedState& state,SurfaceImage* texture,bool& registered,
                                      NativeGeometryResult* result = nullptr);
    void remove_native_static_object(std::uint64_t key,bool terrain = false) noexcept;
    // Draws the set with the current camera and the frame's fog colour/enable
    // (both change without any static object changing).
    HRESULT flush_native_static(const FixedState& frame_state,const std::array<float,16>& world_to_clip);
    HRESULT flush_native_static_terrain(const FixedState& frame_state,const std::array<float,16>& world_to_clip);
    // Per-frame terrain globals (wind, clocks, fog, sun comparison) read by
    // every terrain instance, so registered terrain records stay unchanged.
    HRESULT set_native_terrain_frame(const NativeTerrainState& terrain,bool full_sun,
                                     const std::vector<float>* layer_shifts = nullptr) noexcept;
    // Live per-object terrain layer shifts (game water levels). The shader
    // lifts layer vertices and recomputes liquid depths from them; a change
    // uploads these few floats and re-prepares terrain world vertices on the
    // GPU, never a terrain or liquid rebuild.
    HRESULT set_native_terrain_layer_shifts(const std::vector<float>& shifts) noexcept;
    std::uint64_t native_terrain_layer_shift_revision() const noexcept { return native_terrain_layer_shift_revision_; }
    // The shared renderer wind of the current world frame (weather_wind.h),
    // advanced once per frame by set_native_terrain_frame.
    const WeatherWindGpu& weather_wind() const noexcept { return weather_wind_.gpu; }
    std::uint64_t native_static_generation() const noexcept { return native_static_generation_; }
    // Layered directional shadows: 1 draws the static layer into its cache,
    // 2 composes each dirty map from the cache and draws per-frame casters.
    // Returns the static-layer dirty mask.
    std::uint8_t set_native_shadow_phase(unsigned phase) noexcept;
    std::size_t native_static_draw_count() const noexcept { return native_static_set_.draws.size(); }
    std::size_t native_static_terrain_draw_count() const noexcept { return native_terrain_set_.draws.size(); }
    // Diagnostic: source line of the last native flush return (reported on failure).
    int native_flush_failure_line() const noexcept { return native_flush_failure_line_; }
    int native_flush_failure_line_ = 0;
    int flush_anisotropy_ = -1; // the anisotropy setting while flush_native runs (apply_state), else -1
    const std::array<float,16>* native_frozen_cull_clip() const noexcept { return native_cull_frozen_?&native_cull_clip_:nullptr; }
    bool set_native_world_lighting(const NativeWorldLighting& lighting) noexcept;
    bool world_map_previews_enabled() const noexcept {
        return configured_boolean(debug_overlay_.config(), "world_map_previews", false);
    }
    std::uint64_t world_map_preview_binding_generation() const noexcept { return world_map_preview_generation_; }
    WorldMapPreviewCacheStats world_map_preview_cache_stats() const noexcept { return world_map_preview_.cache_stats(); }
#ifdef EID3D11_EMBEDDED
    void sync_embedded_world_map_preview_option();
#endif
    void precache_world_map_preview(std::uint64_t generation,
        std::vector<WorldMapPreviewSection> sections, std::string failure = {});
    void activate_world_map_preview(std::uint64_t generation,
        std::vector<WorldMapPreviewSection> sections, std::string failure = {});
    // release: the option turned off; also drop the GPU registrations and
    // the loading cache's CPU memory.
    void retire_world_map_preview(bool release = false);
    // Native submission completes modern HUD placement before crossing the
    // scene queue. The render command below receives that complete clip matrix.
    void submit_world_map_preview(std::uint64_t generation, std::uint32_t section,
        const std::array<float,16>& matrix, const std::array<float,3>& tint,
        const std::array<float,3>& border_tint, bool discovered, const FixedState& state);
    void draw_world_map_preview(std::uint64_t generation, std::uint32_t section,
        const std::array<float,16>& matrix, const std::array<float,3>& tint,
        const std::array<float,3>& border_tint, bool discovered, const FixedState& state);
    void begin_world_map_marker();
    void end_world_map_marker();
    void draw_world_map_markers();
    // Scene thread: the same, with the world environment read on the game thread.
    bool set_native_world_lighting(const NativeWorldLighting& lighting,const WorldEnvironment* environment) noexcept;
    // A portrait owns its light only after the world is composited. Its final
    // flush precedes restoring the exact prepared light, without cave reshaping.
    class PortraitLightingScope {
    public:
        PortraitLightingScope(Renderer& renderer,const NativeWorldLighting* lighting) noexcept;
        ~PortraitLightingScope();
        PortraitLightingScope(const PortraitLightingScope&) = delete;
        PortraitLightingScope& operator=(const PortraitLightingScope&) = delete;
        bool ready() const noexcept { return ready_; }
        bool finish() noexcept;
    private:
        Renderer* renderer_ = nullptr;
        NativeWorldLighting previous_{};
        bool previous_valid_ = false;
        bool ready_ = false;
    };
    // world_time: the game's world clock of these lights (emissive-light pulse).
    bool set_native_point_lights(const std::vector<RetainedWorldPointLight>& lights,double world_time=0) noexcept;
    bool native_shadows_enabled() const noexcept { return native_shadows_enabled_; }
    bool native_point_shadows_enabled() const noexcept { return native_point_shadows_enabled_; }
    bool begin_native_shadows(const NativeShadowFrame& frame, std::uint8_t& dirty_mask) noexcept;
    void native_shadow_cull_projection(NativeShadowCullProjection& projection) const noexcept;
    bool begin_native_point_shadow_faces(UINT slot,UINT light_index,const NativePointShadowFrame& faces) noexcept;
    bool end_native_point_shadow_faces(bool commit) noexcept;
    // Shelter map bake (renderer_precipitation.cpp).
    bool begin_native_rain_occlusion(const NativeShadowSlice& slice,UINT side) noexcept;
    bool end_native_rain_occlusion(bool commit) noexcept;
    bool filter_native_rain_shelter() noexcept;
    bool blur_native_rain_shelter() noexcept;
    bool update_native_rain_rings(float seconds,float rain) noexcept;
    bool filter_native_rain_shelter_d3d12() noexcept;
    bool blur_native_rain_shelter_d3d12() noexcept;
    bool update_native_rain_rings_d3d12(float seconds,float rain) noexcept;
    bool begin_native_point_shadow_batch(std::uint64_t& generation,bool* pressure=nullptr) noexcept;
    bool allocate_native_point_shadow(bool overlay,int& slot) noexcept;
    void release_native_point_shadow(std::uint64_t generation,int slot) noexcept;
    void publish_native_point_shadows(const std::vector<NativePointShadowPublication>& lights,
        const NativePointShadowStats& stats) noexcept;
    // Bounce light volume (cave_irradiance.h): filled once per map and
    // quality, then one round-robin slab per frame (colour flushes only),
    // bound for the native pixel shader (t80-83, s13; its constants are the
    // tail of the point-light constants, b5), released with the device.
    void update_cave_irradiance() noexcept;
    void bind_cave_irradiance() noexcept;
    void bind_cave_irradiance_stages(bool pixel,bool vertex,bool compute) noexcept;
    CaveSampleConstants cave_sample_constants() const noexcept;
    void release_cave_irradiance() noexcept;
    void draw_cave_exit_glow() noexcept;  // caves: the daylight haze past each exit
    void draw_cave_exit_glow_d3d12() noexcept;
    void bind_cave_irradiance_d3d12() noexcept;
    void record_native_shadow_source_diagnostics(std::uint64_t world_serial,
        const NativeShadowSourceDiagnostics& values) noexcept;
    bool end_native_shadows(bool commit) noexcept;
    void discard_native() noexcept;
    bool checkpoint_native(NativeQueueCheckpoint& checkpoint) const noexcept;
    void rollback_native(const NativeQueueCheckpoint& checkpoint) noexcept;
    HRESULT draw_native(const NativeGeometryView& view, const FixedState& state,
                        SurfaceImage* texture);
    // Game particles (game_particles.h): billboards expanded, clipped and
    // fogged on the GPU in the stock draw order, at the call's stream position.
    HRESULT draw_game_particles(const GameParticleView& view, const FixedState& state,
                                SurfaceImage* texture);
    // The selection marker and terrain prints are held until every liquid of
    // the world pass has drawn (flushed in present before AO), so the water's
    // scene copy does not hold them and they do not refract (user 2026-10-19).
    HRESULT draw_game_selection_held(const GameSelectionView& view, const FixedState& state,
                                     std::shared_ptr<SurfaceImage> texture);
    HRESULT draw_game_print_run_held(std::vector<GameParticleGpu> vertices, const std::array<float, 16>& object_to_clip,
                                     float clip_half_width, float clip_half_height, const FixedState& state,
                                     std::shared_ptr<SurfaceImage> texture);
    // Game lightning ribbons (game_particles.h): the stock screen-space strip
    // expanded on the GPU at the call's stream position.
    HRESULT draw_game_ribbon(const GameRibbonView& view, const FixedState& state, SurfaceImage* texture);
#ifdef EID3D11_EMBEDDED
    HRESULT draw_embedded_curse(const EmbeddedCurseInput&,const FixedState&,ID3D11ShaderResourceView*,SurfaceImage* image=nullptr);
    struct EmbeddedEffectTexture {
        ID3D11ShaderResourceView* srv = nullptr;
        SurfaceImage* image = nullptr;
        EmbeddedEffectTexture() = default;
        EmbeddedEffectTexture(std::nullptr_t) {}
        explicit operator bool() const noexcept { return srv || image; }
    };
    HRESULT draw_embedded_golem(const EmbeddedGolemInput&,const FixedState&,const std::array<EmbeddedEffectTexture,6>&);
    HRESULT draw_embedded_arena(const EmbeddedArenaInput&,const FixedState&,const std::array<EmbeddedEffectTexture,8>&);
    HRESULT draw_embedded_ribbon(const EmbeddedRibbonInput&, const FixedState&, SurfaceImage*);
    HRESULT draw_embedded_prints(EmbeddedPrintBatch&);
    HRESULT draw_embedded_combat_text(const EmbeddedCombatTextInput&, const FixedState&, SurfaceImage*);
#endif
    // The game's selection marker (game_particles.h): the stock six-point
    // mesh generated on the GPU from the game's point, size and axes.
    HRESULT draw_game_selection(const GameSelectionView& view, const FixedState& state, SurfaceImage* texture);
    // One texture/state run of game terrain prints (game_particles.h): the
    // games' cached print meshes as a world-space triangle list.
    // The game's combat text glyphs (combat_text.h) as GPU-expanded quads.
    HRESULT draw_game_glyphs(const GameGlyphView& view, const FixedState& state, SurfaceImage* texture);
    HRESULT draw_game_print_run(const GameParticleGpu* vertices, UINT count, const std::array<float, 16>& object_to_clip,
                                float clip_half_width, float clip_half_height, const FixedState& state,
                                SurfaceImage* texture);
    void record_capture(std::uint64_t cpu_ticks, std::uint64_t bytes, bool appended) {
        profiler_.add_capture(cpu_ticks, bytes, appended);
    }
    bool capture_profiling_enabled() const { return profiler_.enabled(); }
    // Owned DrawFrameSelection overlay. Drawn over the composited world in
    // submission order; requests made before the world composite wait for it.
    HRESULT queue_screen_rectangle(const ScreenRectangleRequest& request,
                                   const D3DVIEWPORT7& viewport);
    HRESULT queue_cursor(std::uint64_t cache_key, const std::vector<std::uint32_t>& pixels,
                         UINT width, UINT height, const RECT& destination);
    int cursor_scale_percent() const noexcept { return cursor_scale_percent_; }
    // The game's brightness/contrast/gamma ramp (IDirectDrawGammaControl on the
    // primary surface; game.exe 00504A60). 256 WORDs per channel; any thread.
    void set_gamma_ramp(const WORD* red, const WORD* green, const WORD* blue) noexcept;
    void gamma_ramp(WORD* red, WORD* green, WORD* blue) const noexcept;
    // While the loading picture is held the game cursor is the system cursor
    // (cursor_image.h show_load_cursor); not while renderer controls own input.
    bool load_cursor_wanted() const noexcept { return load_reveal_holding_ && !debug_overlay_.captures_input(); }
    bool owned_ui_text_enabled() const noexcept;
    // interface_space: an interface surface; inside an anchored HUD widget it
    // is composed at the UI scale around the widget's pivot (widescreen::hud_blit).
    // A host-owned UI texture (same device) drawn in the interface pass,
    // alpha-blended unless opaque: the embedded host's SpriteBatch path.
    HRESULT blit_srv(ID3D11ShaderResourceView* image, const RECT& destination,
                     const RECT* source_rect, bool opaque);
    HRESULT blit(const SurfaceImage& source, const RECT& destination, const RECT* source_rect,
                 const DDCOLORKEY* source_key, bool interface_space = false, bool native_canvas = true);
    // A full-screen picture (loading screen, movie frame) shown at its own
    // aspect: pillarboxed or letterboxed over a black or blurred fill
    // (`pillarbox_fill`). A source of the output's aspect fills the screen.
    // `placed`: the game placed the picture itself (a letterboxed movie at
    // 3840x1632 of a 3840x2160 primary); it keeps that rectangle and the
    // rest of the screen gets the same fill.
    HRESULT present_image(const SurfaceImage& source, const RECT* source_rect, const DDCOLORKEY* source_key,
                          const RECT* placed = nullptr);
    // A large picture reached the primary this frame (loading screens, movies,
    // also when not exactly full-screen): the load reveal keeps the latest one.
    void mark_load_picture() noexcept { if(scene_queue()){scene_post_command(scene_cmd::MarkLoadPicture{});return;} load_picture_this_frame_ = true; }
#ifdef EID3D11_EMBEDDED
    // Immutable host-decoded loading frame; queue commands retain its pixels.
    void set_embedded_movie_image(std::shared_ptr<const SurfaceImage> image);
#endif
    void retire_surface(std::uint64_t surface_id) noexcept;
    void retire_camp_texture(std::uint64_t surface_id,
                             std::uint64_t generation) noexcept;
    // interface_space: the destination is interface space and takes the HUD
    // composition, as a UI surface blit does; camp-mapped pieces do not.
    // observe_scale: a composed-scale mismatch updates the shared raster
    // scale (pane and chat); retained card text keeps its own per surface.
    HRESULT queue_character_pane_text(
        const CharacterPaneTextFrame& frame, std::uint64_t ui_epoch,
        std::uint64_t frame_serial, const RECT& destination,
        const RECT* source_rect, bool interface_space = true,
        bool observe_scale = true,
        const CharacterPaneTextFit* fit = nullptr);
    // A retained text surface presented where its blit would land: the same
    // camp decoration and piece mapping as blit(), then the glyphs. Sets
    // composed_scale to the output pixels per surface pixel (vertical) it was
    // placed at, 0 when nothing was placed, for that surface's next raster.
    HRESULT queue_retained_owned_text(
        const CharacterPaneTextFrame& frame, std::uint64_t ui_epoch,
        std::uint64_t frame_serial, const RECT& destination,
        const RECT* source_rect, float& composed_scale,
        const CharacterPaneTextFit* fit = nullptr);
    // Output pixels per surface pixel that owned text drawn now will be
    // composed at (the current HUD composition times output/canvas), so its
    // glyphs can be rasterized at that size. Game thread. Glyphs rasterize at
    // the vertical scale the pane text was last actually composed at (its
    // blit can scale the pane surface too, which the composition alone does
    // not show); before the first blit, the composition's vertical scale.
    // Stock-size glyphs scaled one by one made letter heights jump (#185).
    float owned_text_raster_scale() const noexcept {
        if (observed_text_scale_ > 0.f) return observed_text_scale_;
        const auto& h = widescreen::interface_owned ? widescreen::hud_blit : widescreen::HudComposition{};
        return h.scale_y * float(output_height_) / float(canvas_height());
    }
    HRESULT capture(SurfaceImage& destination, const RECT& destination_rect,
                    const RECT& source_rect);
    HRESULT capture(SurfaceImage& destination, LONG destination_x,
                    LONG destination_y, const RECT& source_rect) {
        const RECT destination_rect{
            destination_x, destination_y,
            destination_x + source_rect.right - source_rect.left,
            destination_y + source_rect.bottom - source_rect.top};
        return capture(destination, destination_rect, source_rect);
    }
    HRESULT readback(SurfaceImage& image);
    HRESULT upload(SurfaceImage& image, SurfaceUploadResult* result = nullptr);
    // Copy-on-write for a texture the owned cache shares: before an in-place
    // GPU write, the surface moves to an exact copy and the cache keeps the
    // old texture for its generation. One bridge event per rewrite (rare).
    HRESULT detach_shared_surface_texture(SurfaceImage& image, const char* writer);
    // The surface no longer uses `texture`: its cache entries own it alone.
    void unshare_owned_texture(SurfaceImage& image, ID3D11Texture2D* texture) noexcept;
    // Scene thread (render_thread option), copy-on-write across threads. The
    // owned cache (scene thread) may share a surface's GPU home only once the
    // game thread has published it (scene_publish_surface_texture): from then
    // on the game thread never writes that texture in place. Its writers
    // (game_upload, capture) first move the surface to a copy recorded on the
    // game-upload deferred context, which the scene thread executes in stream
    // order before any later work, and queue the unshare of the old texture
    // behind every command that may have shared it.
    void scene_publish_surface_texture(SurfaceImage& image) noexcept;
    HRESULT scene_detach_surface_texture(SurfaceImage& image, const char* writer);
    void scene_unshare_surface_texture(std::uint64_t id, ID3D11Texture2D* texture) noexcept;
    // Scene thread: whether the game thread still uses `texture` as surface
    // id's GPU home (compared only, never dereferenced).
    bool scene_surface_holds(std::uint64_t id, ID3D11Texture2D* texture) const noexcept;
    // Returns a released GPU-primary surface's exact pixels to the CPU by a
    // one-shot staging readback. A surface restored twice within
    // kSurfaceRepeatReadFrames stays CPU-resident (cpu_pinned) so a hot reader
    // never costs a readback per frame; pin_now does the same at once.
    HRESULT restore_surface_pixels(SurfaceImage& image, bool pin_now = false);
    static constexpr std::uint64_t kSurfaceRepeatReadFrames = 120;
    static constexpr std::uint8_t kSurfaceCpuGraceFrames = 2;
    // Drops a draw-only GPU-primary generation after its sampling grace period.
    void release_surface_pixels(SurfaceImage& image) noexcept;
    // Presents a surface must go without CPU access before an unsampled
    // generation drops its CPU copy (the proxy sweeps; #188).
    static constexpr std::uint64_t kSurfaceCpuIdlePresents = 300;
    void release_idle_surface_pixels(SurfaceImage& image) noexcept;
    void sampled_surface_pixels(SurfaceImage& image,bool resolved=false) noexcept;
    // Completed proxy sources carry no pixels. The proxy resolves a current
    // surface generation to its texture, uploading it if needed. `image`
    // receives the surface's image, so the owned cache can mark a texture it
    // shares (SurfaceImage::texture_shared).
    using SurfaceTextureResolver = HRESULT (*)(void* context, ID3D11Device* device,
        std::uint64_t id, std::uint64_t generation, ComPtr<ID3D11Texture2D>& texture,
        SurfaceImage*& image);
    void set_surface_texture_resolver(SurfaceTextureResolver resolver, void* context) noexcept {
        surface_texture_resolver_ = resolver; surface_texture_resolver_context_ = context;
    }
    HRESULT surface_effect_d3d12(const SurfaceImage& source, const SurfaceImage& destination,
                                 const RECT& viewport, bool fade, std::vector<std::uint8_t>& pixels);
    HRESULT composite_world();
    void queue_camp_decoration(const CampDecoration& frame, bool include_popup = true) noexcept;
    HRESULT begin_camp_popup(bool clear);
    void end_camp_popup(bool complete) noexcept;
    void invalidate_camp_popup() noexcept;
    void reset_camp_popup() noexcept;
    HRESULT flush_ui();
    // Scene mode, game thread: whether UI work the game thread queued may still
    // be pending on the scene thread (it cannot read the scene's queues).
    bool has_pending_ui() const noexcept { return scene_queue() ? scene_ui_pending_ : (!pending_blits_.empty() || cursor_pending_); }
    HRESULT present();
    void release();
    // Direct embedded-host seam; active targets are acquired at each call,
    // including the scene-to-interface boundary and render-scale changes.
    HRESULT visit_embedded_device(void* user, EmbeddedDeviceVisitor visitor) const {
        return owned::visit_embedded_device(device_.Get(), context_.Get(),
            rtv_.Get(), dsv_.Get(), user, visitor);
    }
    // Current-target operations, without a borrowed backend device or the
    // legacy Clear operation's implicit world-composition boundary.
    HRESULT bind_embedded_targets();
    HRESULT clear_embedded_targets(const float* color, const float* depth);
    HRESULT read_embedded_viewport(D3DVIEWPORT7& viewport);
    HRESULT read_embedded_output_size(UINT& width, UINT& height);
    // Native output capture, tightly packed BGRA. S_FALSE means minimized.
    // The caller must consume the returned pixels only on S_OK.
    HRESULT read_embedded_d3d12_output(std::vector<std::uint8_t>& pixels, UINT& width, UINT& height);
    bool embedded_hit_test_frame() const noexcept { return hit_test_active_; }
    HRESULT visit_embedded_output(void* user, EmbeddedOutputVisitor visitor) const {
        if (hit_test_failed_) return E_FAIL;
        return owned::visit_embedded_output(device_.Get(), context_.Get(),
            hit_test_active_ ? hit_test_rtv_.Get() : backbuffer_rtv_.Get(), user, visitor);
    }
    // ---- Scene thread (render_thread option; scene_thread.h) ----------------
    // Off (the default): scene_queue() is false everywhere and every call runs
    // inline on its caller's thread exactly as before.
    // On: the scene thread owns this renderer (immediate context, swap chain,
    // every queue and cache). Game-thread code queues its work with
    // scene_post (copied inputs only, never game pointers) or, on a path not
    // converted, waits for the scene thread with scene_sync. Public entry
    // points that touch renderer state begin with SCENE_ENTRY, so an
    // unconverted caller stays correct, only synchronous (each such reason is
    // logged once as render_thread_sync and counted in the summary).
    bool scene_mode() const noexcept { return scene_mode_.load(std::memory_order_acquire); }
    bool on_scene_thread() const noexcept { return scene_thread_ && scene_thread_->on_thread(); }
    bool scene_queue() const noexcept { return scene_mode() && !on_scene_thread() && !scene_inline_; }
    // Game thread: drains the scene thread (reason non-null and queuing) and
    // keeps every renderer call inline until the scope ends; the scene thread
    // stays parked meanwhile (nothing is posted).
    struct SceneInlineScope {
        Renderer* renderer;
        bool active;
        SceneInlineScope(Renderer* owner, const char* reason) noexcept
            : renderer(owner), active(reason && owner->scene_queue()) {
            if (active) { owner->scene_sync(reason); ++owner->scene_inline_; }
        }
        ~SceneInlineScope() { if (active) --renderer->scene_inline_; }
        SceneInlineScope(const SceneInlineScope&) = delete;
        SceneInlineScope& operator=(const SceneInlineScope&) = delete;
    };
    // Core mode (EI_RENDERER_X64_CORE, x86 only): a closure cannot cross to
    // the host. A post that is not a typed core command is refused: logged
    // once by its site, counted per 600 frames, its visual omitted.
    template <class F> void scene_post(F&& work, const char* site = __builtin_FUNCTION()) {
        if (scene_queue()) {
            if (core_mode_) { core_refuse(site); return; }
            if (game_upload_pending_) flush_game_uploads();
            if (scene_thread_->post(std::forward<F>(work))) return;
            // Allocation failed: run it here, in order, with the scene thread
            // drained (scene_inline_ makes the call take its inline body).
            const SceneInlineScope inline_scope(this, "post_allocation_failed");
            work();
            return;
        }
        work();
    }
    // As scene_post, with the bridge's per-frame values of this point of the
    // frame installed on the scene thread before the work runs.
    // The game thread's HUD composition (widescreen::hud_blit, thread_local:
    // the anchored panel/slot's offset, scale and panel clip) travels with the
    // command too; the scene thread's own copy is identity, so without it
    // queued interface work lost its placement and slid-out panel clip.
    template <class F> void scene_post_frame(F&& work, const char* site = __builtin_FUNCTION()) {
        if (!scene_queue()) { work(); return; }
        if (core_mode_) { core_refuse(site); return; }
        BridgeFrameValues values;
        if (!bridge_capture_frame_values(values)) return;
        scene_post([this, values = std::move(values), hud = widescreen::hud_blit,
                    work = std::forward<F>(work)]() mutable {
            if (!on_scene_thread()) { work(); return; }
            bridge_install_frame_values(values);
            const auto previous = widescreen::hud_blit;
            widescreen::hud_blit = hud;
            work();
            widescreen::hud_blit = previous;
        }, site);
    }
    // A typed scene command (scene_commands_world.h): queued as above, with
    // the frame values when the command declares frame_values. The Renderer
    // is supplied at run time; the command itself holds values only. Core
    // mode encodes it for the host (scene_core_post.h) or refuses it.
    template <class C> void scene_post_command(C command, const char* site = __builtin_FUNCTION()) {
        if (core_mode_ && scene_queue()) { core_post_command(command, site); return; }
        if constexpr (C::frame_values)
            scene_post_frame([this, command = std::move(command)]() mutable { command.run(*this); }, site);
        else
            scene_post([this, command = std::move(command)]() mutable { command.run(*this); }, site);
    }
    // ---- Core mode (EI_RENDERER_X64_CORE=1, S3) -------------------------------
    // The x86 Renderer has no device: it runs the game-thread halves and sends
    // typed commands to the host's Renderer. False in the host and in the
    // standard and call-stream modes.
    bool core_mode() const noexcept { return core_mode_; }
    // Host, core mode: the UI flush dropped cached quads the game process's
    // overlay mirror still claims (core_host publishes it through Control).
    bool take_overlay_mirror_reset() noexcept { return overlay_mirror_reset_.exchange(false, std::memory_order_acq_rel); }
    // Core-mode host: the game process left through its clean exit
    // (Control::game_clean_exit); the host, the only ini writer, clears the
    // over-budget marker as the game's own clean exit does in standard mode.
    void host_game_exited_cleanly() noexcept { debug_overlay_.clear_over_budget_marker(); }
    // Host, core mode: a pixel-free snapshot of the game surface (id,
    // generation) from the surface table; null (logged) when the host does not
    // hold that generation. Used while decoding a command's surface handles.
    std::shared_ptr<SurfaceImage> core_surface_snapshot(std::uint64_t id, std::uint64_t generation);
    // Host, core mode: the completed visual surface (id, generation) from the
    // table for the owned texture cache (the SurfaceTextureResolver contract);
    // E_PENDING when the host does not hold that generation yet.
    HRESULT core_resolve_surface(std::uint64_t id, std::uint64_t generation, ComPtr<ID3D11Texture2D>& texture,
                                 SurfaceImage*& image) noexcept;
    // Host, core mode: a GPU home the host made itself (a camp icon its icon
    // worker decoded) becomes the game surface's table entry.
    void core_adopt_surface(std::shared_ptr<SurfaceImage> image) {
        if (image) { const auto id = image->id; core_surfaces_[id] = std::move(image); }
    }
    template <class C> void core_post_command(const C& command, const char* site);  // scene_core_post.h
    void core_refuse(const char* site) noexcept;
    // Host: core mode's frame start on its own device and swap chain (the
    // game window's child the host created). ensure() minus device creation.
    HRESULT ensure_host(HWND window, UINT width, UINT height);
    // Game thread, scene mode: native or UI work was queued (proxy flushes
    // queue a flush only when something may be pending).
    void scene_note_native() noexcept { scene_native_pending_ = true; }
    bool scene_native_pending() const noexcept { return scene_native_pending_; }
    void scene_note_ui() noexcept { scene_ui_pending_ = true; }
    void release_native_checkpoint(const NativeQueueCheckpoint& checkpoint) noexcept;
    void scene_sync(const char* reason) noexcept;
    // Game thread, scene mode: the surface's current GPU home (uploaded on the
    // game thread if needed) as a pixel-free snapshot a queued command may use.
    // Null for null. Off (or on the scene thread) it returns a non-owning
    // pointer to the image itself.
    std::shared_ptr<SurfaceImage> scene_surface(SurfaceImage* image);
    // Core mode: ready once the host link is up (no device in this process).
    bool ready() const {
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
        if (d3d12_) return !embedded_d3d12_failed_;
#endif
        return device_ != nullptr || core_mode_ || d3d12_ != nullptr;
    }
    // S_OK while the device works; the DXGI removal reason after a driver
    // timeout (TDR) or reset. Callers query it only after a failed HRESULT.
    // Core mode: the host link's loss (named in the device-removed box).
    HRESULT device_removed_reason() const noexcept {
        if (core_mode_) return core_removed_reason();
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
        if (d3d12_) return embedded_d3d12_.device_removed_reason();
#endif
        return device_ ? device_->GetDeviceRemovedReason() : S_OK;
    }
    HRESULT core_removed_reason() const noexcept;
    // One bridge event naming the removal reason, adapter, driver and memory.
    void report_device_removed(HRESULT reason) noexcept;
    ID3D11Device* resource_device() const noexcept { return device_.Get(); }
    void enable_gpu_journal() noexcept { profiler_.enable_gpu_journal(); }
    std::string take_gpu_pass_report(std::string* frame_waits = nullptr) {
        scene_sync("gpu_pass_report");
        return profiler_.take_gpu_pass_report(frame_waits);
    }
    D3D12Frame::FrameGenerationStats frame_generation_stats() const noexcept {
        return d3d12_ ? d3d12_->frame_generation_stats() : D3D12Frame::FrameGenerationStats{};
    }
    ProfileTimestamps* native_profile_timestamps() const noexcept {
        return d3d12_ ? &d3d12_->profile_timestamps() : nullptr;
    }
    UINT width() const { return width_; }
    UINT height() const { return height_; }
    std::uint64_t native_logical_draws() const { return perf_native_draws_; }
    std::uint64_t native_physical_batches() const { return perf_native_batches_; }
    std::uint64_t native_compute_dispatches() const { return perf_native_compute_dispatches_; }
#ifdef EID3D11_TESTING
    bool test_arm_shadow_temporal_pair() noexcept;
    const RenderProfileSnapshot* test_render_profile_snapshot() const noexcept {
        return profiler_.latest_snapshot();
    }
    HRESULT test_render_debug_resource_view(DebugResourceView view,
                                            DebugOverlayMode mode) noexcept;
    void test_attach_debug_resource_view_device(
        ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11RenderTargetView* render_target, ID3D11Texture2D* depth,
        ID3D11DepthStencilView* depth_view,
        ID3D11ShaderResourceView* depth_resource,
        ID3D11ShaderResourceView* ao_resource,
        UINT width, UINT height) noexcept;
    void test_set_debug_projection(const D3DMATRIX& projection) noexcept;
    void test_attach_debug_shadow_maps(ID3D11ShaderResourceView* maps,
                                      NativeShadowFrame frame,
                                      std::uint8_t valid_mask,
                                      std::uint8_t rewritten_mask) noexcept;
    DebugShadowInfo test_debug_shadow_info() const noexcept { return debug_shadow_info(); }
    void test_set_debug_view(DebugResourceView view, DebugOverlayMode mode) noexcept {
        debug_overlay_.config().set_debug_resource_view(view);
        debug_overlay_.config().set_overlay_mode(mode);
    }
    DebugResourceViewAvailability test_debug_resource_view_availability() const noexcept;
    DebugResourceViewWorkSnapshot test_debug_resource_view_work() const noexcept {
        return debug_resource_view_work_;
    }
    HRESULT test_reset_native_geometry_arena(UINT capacity);
    HRESULT test_reclaim_native_geometry_working_set();
    HRESULT test_reserve_native_geometry(UINT required);
    void test_set_native_geometry_page_limit(UINT limit) noexcept {
        test_native_geometry_page_limit_ = limit;
    }
    void test_fail_native_geometry_allocations(UINT buffer_failures,
                                               UINT view_failures) noexcept {
        test_native_geometry_buffer_failures_ = buffer_failures;
        test_native_geometry_view_failures_ = view_failures;
    }
    UINT test_native_geometry_capacity() const noexcept;
    UINT test_native_geometry_used() const noexcept;
    std::uint64_t test_native_geometry_capacity64() const noexcept {
        return native_geometry_capacity_bytes();
    }
    std::uint64_t test_native_geometry_used64() const noexcept {
        return native_geometry_high_water_bytes();
    }
    std::uint64_t test_native_geometry_live() const noexcept {
        return native_geometry_live_bytes_;
    }
    std::uint64_t test_native_geometry_free() const noexcept;
    UINT test_native_geometry_page_count() const noexcept {
        return static_cast<UINT>(native_geometry_pages_.size());
    }
    bool test_native_mesh_span(std::uintptr_t cache_identity,
                               UINT& page, UINT& offset, UINT& bytes) const noexcept;
    void test_advance_native_geometry_frame() noexcept { ++native_frame_index_; }
    void test_fail_native_geometry_upload(UINT countdown) noexcept {
        test_native_geometry_upload_failure_ = countdown;
    }
    HRESULT test_reserve_native_submission(UINT draw_count, UINT batch_count) {
        return ensure_native_stream_buffers(16, draw_count, 0, 0, batch_count);
    }
    void test_fail_native_draw_work_allocations(UINT buffer_failures,
                                                UINT view_failures) noexcept {
        test_native_draw_work_buffer_failures_ = buffer_failures;
        test_native_draw_work_view_failures_ = view_failures;
    }
    void test_fail_native_indirect_allocations(UINT buffer_failures,
                                               UINT view_failures) noexcept {
        test_native_indirect_buffer_failures_ = buffer_failures;
        test_native_indirect_view_failures_ = view_failures;
    }
    UINT test_native_draw_work_capacity() const noexcept {
        return native_draw_work_capacity_;
    }
    UINT test_native_indirect_args_capacity() const noexcept {
        return native_indirect_args_capacity_;
    }
    bool test_native_dispatch_grid(UINT count, UINT& width, UINT& height) const noexcept;
    std::size_t test_native_page_dispatch_count(bool shadows) const noexcept {
        return native_submission_layouts_[shadows ? 1 : 0].dispatches.size();
    }
    std::size_t test_native_geometry_page_dispatch_count(bool shadows) const noexcept {
        return native_submission_layouts_[shadows ? 1 : 0].geometry_dispatches.size();
    }
    void test_set_native_work_prefix(UINT prefix, bool shadows) noexcept {
        test_native_work_prefix_[shadows ? 1 : 0] = prefix;
        native_layout_changed();
    }
    HRESULT test_seed_native_prepared_slot_boundaries(UINT mesh_slots,
                                                      UINT material_slots,
                                                      bool first_reusable = false);
    void test_reset_native_prepared_slot_visits() noexcept {
        test_native_prepared_mesh_slot_visits_ = 0;
        test_native_prepared_material_slot_visits_ = 0;
    }
    std::uint64_t test_native_prepared_mesh_slot_visits() const noexcept {
        return test_native_prepared_mesh_slot_visits_;
    }
    std::uint64_t test_native_prepared_material_slot_visits() const noexcept {
        return test_native_prepared_material_slot_visits_;
    }
    bool test_native_prepared_record_addresses_current(
        const NativePreparedMeshHandle& mesh,
        const NativePreparedMaterialHandle& material,
        bool shadows = false) const noexcept;
    std::size_t test_native_mesh_count() const { return native_meshes_.size(); }
    std::size_t test_native_pending_draw_count() const {
        return native_pending_draws_.size();
    }
    std::uint64_t test_native_generic_ingress_calls() const {
        return native_generic_ingress_calls_;
    }
    std::uint64_t test_native_prepared_ingress_calls() const {
        return native_prepared_ingress_calls_;
    }
    RendererResourceSnapshot test_resource_snapshot() const {
        return resource_snapshot();
    }
    std::size_t test_pending_character_pane_quads() const noexcept;
    std::size_t test_pending_character_pane_upload_bytes() const noexcept;
    std::size_t test_last_character_pane_draw_calls() const noexcept {
        return test_last_character_pane_draw_calls_;
    }
    void test_fail_next_character_pane_text_srv_creation() noexcept {
        test_fail_character_pane_text_srv_creation_ = true;
    }
    bool test_character_pane_overlay_cache_empty(
        std::uint64_t source_identity) const noexcept;
    bool test_first_character_pane_destination(
        RECT& destination) const noexcept;
    void test_fail_next_native_specular_color_upload() noexcept {
        test_fail_native_specular_color_upload_ = true;
    }
    std::uint64_t test_native_prepared_texture_generation(
        const NativePreparedMaterialHandle& handle) const;
    void test_invalidate_native_prepared_handles() noexcept;
    std::uint64_t test_native_world_lighting_uploads() const noexcept {
        return native_world_lighting_uploads_;
    }
    const NativeWorldLighting& test_native_world_lighting() const noexcept {
        return native_world_lighting_;
    }
    std::uint64_t test_native_world_prepared_vertices() const;
    std::uint64_t test_native_pose_nodes_evaluated() const { return test_native_pose_nodes_evaluated_; }
    std::uint64_t test_native_instance_rows_uploaded() const { return test_native_instance_rows_uploaded_; }
    std::uint64_t test_native_command_values_written() const { return test_native_command_values_written_; }
    std::uint64_t test_native_depth_selections() const { return test_native_depth_selections_; }
    std::size_t test_native_static_shadow_record_count() const {
        return native_static_shadow_records_.size();
    }
    std::uint64_t test_native_depth_registrations() const { return test_native_depth_registrations_; }
    std::uint64_t test_native_descriptors_uploaded() const { return test_native_descriptors_uploaded_; }
    std::uint64_t test_native_work_upload_bytes() const { return test_native_work_upload_bytes_; }
    std::uint64_t test_native_material_groups_created() const { return test_native_material_groups_created_; }
    std::uint64_t test_native_material_group_trim_scans() const { return test_native_material_group_trim_scans_; }
    std::uint64_t test_native_material_reconstructions() const { return test_native_material_reconstructions_; }
    std::uint64_t test_native_prepared_material_hits() const { return test_native_prepared_material_hits_; }
    std::size_t test_native_live_material_groups() const { return native_state_lookup_.size(); }
    struct NativePendingWorkTestSnapshot {
        std::size_t dirty = 0, poses = 0, phases = 0, records = 0, record_slots = 0, groups = 0;
        std::size_t pose_slots = 0, phase_slots = 0, pose_ranges = 0, world_ranges = 0, animation_ranges = 0;
        std::size_t upload_bytes = 0;
    };
    NativePendingWorkTestSnapshot test_native_pending_work() const noexcept {
        NativePendingWorkTestSnapshot value;
        value.dirty=native_instance_dirty_.size();value.poses=native_pose_work_.size();
        value.phases=native_terrain_phase_work_.size();value.record_slots=native_instance_records_.size();
        for(const auto& record:native_instance_records_)value.records+=record.occupied?1u:0u;
        value.groups=native_state_lookup_.size();value.pose_slots=native_pose_slots_.size();
        value.phase_slots=native_terrain_phases_.size();value.pose_ranges=native_pose_ranges_.size();
        value.world_ranges=native_world_ranges_.size();value.animation_ranges=native_animation_ranges_.size();
        value.upload_bytes=native_pending_upload_.size();return value;
    }
    void test_fail_native_enqueue_after(UINT successful) noexcept { test_native_enqueue_failure_=successful+1; }
    std::uint64_t test_native_layout_rebuilds() const { return test_native_layout_rebuilds_; }
    std::uint64_t test_native_terrain_phase_upload_bytes() const { return test_native_terrain_phase_upload_bytes_; }
    bool test_native_record_state(const NativePreparedMaterialHandle& handle,FixedState& state) const {
        if(handle.slot>=native_prepared_material_slots_.size())return false;
        const auto& slot=native_prepared_material_slots_[handle.slot];
        if(!slot.active||slot.generation!=handle.generation||slot.persistent_instance[0]>=native_instance_records_.size())return false;
        const auto& draw=native_instance_records_[slot.persistent_instance[0]].command;
        state=native_state_buckets_[draw.state_bucket].state;return true;
    }
    std::size_t test_native_pending_command_bytes() const {
        return native_pending_draws_.size()*sizeof(native_pending_draws_[0]);
    }
    std::uint64_t test_native_world_reused_records() const { return native_world_reused_records_; }
    UINT test_native_world_capacity() const { return native_world_vertex_capacity_; }
    UINT test_native_world_used() const { return native_world_vertex_used_; }
    std::uint64_t test_native_world_allocation_attempts() const { return test_native_world_allocation_attempts_; }
    HRESULT test_reserve_native_world(UINT required) { return ensure_native_world_buffer(required); }
    HRESULT test_reclaim_native_world() { return reclaim_native_world_ranges(); }
    UINT test_native_world_base(const NativePreparedMeshHandle& handle) const {
        return handle.slot<native_prepared_mesh_slots_.size() &&
            native_prepared_mesh_slots_[handle.slot].generation==handle.generation
            ?native_prepared_mesh_slots_[handle.slot].world_base:UINT_MAX;
    }
    void test_fail_native_world_allocation(bool fail) { test_native_world_allocation_failure_=fail; }
    std::uint64_t test_native_shadow_draws() const { return native_shadow_draws_; }
    std::uint64_t test_native_shadow_triangles() const { return native_shadow_triangles_; }
    std::uint64_t test_native_shadow_reused_slices() const { return native_shadow_reused_slices_; }
    std::uint64_t test_native_shadow_value_reuses() const { return native_shadow_value_reuses_; }
    std::uint64_t test_native_shadow_validity_scans() const { return native_shadow_validity_scans_; }
    std::uint64_t test_native_shadow_frame_copies() const { return native_shadow_frame_copies_; }
    PlacedHitchShadowDecision test_shadow_decision() const { return native_frame_stats_.shadow_decision; }
    std::uint64_t test_native_owned_texture_uploads() const { return native_owned_texture_uploads_; }
    std::uint64_t test_native_owned_array_uploads() const { return native_owned_array_uploads_; }
    std::uint64_t test_native_prepared_mesh_hits() const { return native_prepared_mesh_hits_; }
    std::uint64_t test_native_owned_texture_bytes(bool arrays) const;
    void test_native_owned_texture_budget(std::uint64_t bytes) { test_native_owned_texture_budget_=bytes; }
    void test_native_shadow_budget(std::uint64_t bytes) { test_native_shadow_budget_=bytes; }
    std::uint64_t test_native_shadow_bytes() const {
        return native_shadow_fixed_bytes()+native_owned_texture_bytes(true);
    }
    std::uint8_t test_native_shadow_valid_mask() const {
        std::uint8_t mask=0;
        for(unsigned i=0;i<native_shadow_valid_.size();++i) if(native_shadow_valid_[i]) mask|=1u<<i;
        return mask;
    }
    bool test_native_shadow_depth(UINT slice, UINT x, UINT y, float& depth) noexcept;
    void test_set_shadow_history_epoch(std::uint64_t epoch) noexcept {
        test_shadow_history_epoch_=epoch;
    }
    const NativeShadowFrame& test_native_shadow_frame() const { return native_shadow_frame_; }
    std::uint64_t test_native_contribution_rejects() const { return native_contribution_rejects_; }
    std::uint64_t test_native_owned_texture_creation_attempts() const { return native_owned_texture_serial_; }
    bool test_native_world_vertex(const NativePreparedMeshHandle& handle, UINT component,
        std::array<float, 3>& position, std::array<float, 3>& normal) noexcept;
    std::vector<UINT> test_native_pending_terrain_phase_offsets() const {
        std::vector<UINT> offsets;
        for(const auto& selected:native_pending_draws_)offsets.push_back(native_instance_records_[selected.record].command.instance.terrain_phase_offset);
        return offsets;
    }
    bool test_native_terrain_phase_word(UINT offset,UINT& value) noexcept;
    bool test_native_world_lighting_dirty() const noexcept {
        return native_world_lighting_dirty_;
    }
    bool test_native_world_lighting_valid() const noexcept {
        return native_world_lighting_valid_;
    }
    void test_fail_next_native_flush(bool after_lighting_upload) noexcept {
        test_native_flush_failure_ = after_lighting_upload ? 2 : 1;
    }
    void test_enable_color_resource_diagnostic(bool value) noexcept {
        test_color_resource_diagnostic_ = value;
    }
    void test_color_diagnostic_shadow_state(bool value) noexcept {
        native_shadow_collecting_ = value;
    }
    bool test_color_resource_diagnostic_clean() const noexcept {
        return !active_color_resource_ && !color_mesh_active_ &&
            !color_texture_active_ && !color_cold_active_;
    }
    PlacedHitchRendererFrame::ColorResourceDiagnostic test_color_resource_diagnostic() const noexcept {
        return native_frame_stats_.color_resource;
    }
    bool test_has_debug_option(const char* key) const noexcept {
        return debug_overlay_.config().find(key) != nullptr;
    }
    bool test_set_debug_boolean(const char* key, bool value) noexcept {
        return debug_overlay_.config().set_boolean(key, value);
    }
    bool test_set_debug_integer(const char* key, int value) noexcept {
        return debug_overlay_.config().set_integer(key, value);
    }
    UINT test_native_shadow_side() const noexcept { return native_shadow_side_; }
    UINT test_native_point_shadow_side() const noexcept { return native_point_shadow_side_; }
    UINT test_native_shadow_texture_side() const noexcept {
        if(!native_shadow_depth_)return 0;
        D3D11_TEXTURE2D_DESC desc{};native_shadow_depth_->GetDesc(&desc);return desc.Width;
    }
    UINT test_native_point_shadow_texture_side() const noexcept {
        return native_point_shadow_pages_.empty()?0:native_point_shadow_side_;
    }
#endif

private:
#ifdef EID3D11_EMBEDDED
    HRESULT compose_embedded_figure_texture(ID3D11ShaderResourceView* primary,
        ID3D11ShaderResourceView* secondary, ComPtr<ID3D11ShaderResourceView>& combined);
    struct EmbeddedFigureLayers {
        std::uint64_t primary_generation=0,secondary_generation=0;
        ComPtr<ID3D11ShaderResourceView> texture;
        D3D12TextureRef texture12;
    };
    HRESULT compose_embedded_figure_texture_d3d12(const D3D12TextureRef& primary,
        const D3D12TextureRef& secondary, D3D12TextureRef& combined);
    std::map<std::pair<std::uint64_t,std::uint64_t>,EmbeddedFigureLayers> embedded_figure_layers_;
    ComPtr<ID3D11VertexShader> embedded_figure_vs_;
    ComPtr<ID3D11PixelShader> embedded_figure_ps_;
    ComPtr<ID3D11SamplerState> embedded_figure_sampler_;
#endif

    void set_test_initialization_stage(const char* stage) noexcept {
        test_initialization_failure_stage_ = stage;
        if (test_initialization_observer_)
            test_initialization_observer_(test_initialization_observer_context_, stage);
    }
    const char* test_initialization_failure_stage_ = "not-started";
    TestInitializationObserver test_initialization_observer_ = nullptr;
    void* test_initialization_observer_context_ = nullptr;
    GpuBlitKey gpu_blit_key_;
    struct PendingBlit { std::uint64_t source_id{}, cache_key{}, generation{}, base_generation{}, revision{}, ui_epoch{}, frame_serial{}; bool native_canvas{true}; bool output_space{}, filtered_text{}; bool keyed{}, gpu{}, cached{}, partial{}, force_opaque_alpha{}, batchable_text{}, source_is_surface{}, source_destroyed{}, quarter_turn{}, backdrop{}, presentation_fill{}; widescreen::HudComposition composition{}; RECT source{}, destination{}, update{}; DDCOLORKEY color_key{}; UINT width{}, height{}, pitch{}; std::uint32_t diffuse{0xffffffffu}; float u0{}, v0{}, u1{1}, v1{1}; std::vector<std::uint32_t> pixels; std::shared_ptr<const CharacterPaneGlyphAsset> text_asset; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv; D3D12TextureRef d3d12; };
    struct OverlayTexture { UINT width{}, height{}; std::uint64_t generation{}, revision{}, surface_id{}; RECT source{}; bool keyed{}; DDCOLORKEY color_key{}; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv; D3D12TextureRef d3d12;
        bool resident() const noexcept { return texture || d3d12; } };
    // Scene mode: the game thread's view of overlay_textures_ (what the cache
    // will hold once its queued blits ran), used to choose cached/partial
    // conversions exactly as blit() does. A drop on the scene side resets it.
    struct OverlayMirror { std::uint64_t surface_id{}, generation{}; UINT width{}, height{}; RECT source{}; bool keyed{}; DDCOLORKEY color_key{}; };
    std::unordered_map<std::uint64_t, OverlayMirror> overlay_mirror_;   // game thread
    std::atomic<bool> overlay_mirror_reset_{false};
    HRESULT make_scene_blit(const SurfaceImage& source, const RECT& destination, const RECT* source_rect,
                            const DDCOLORKEY* source_key, bool interface_space, bool native_canvas,
                            std::vector<PendingBlit>& output);
    PendingBlit make_presentation_fill(const PendingBlit& picture, const widescreen::PresentationFit& fit) const;
    struct NativeTextureArrayKey {
        UINT width = 0;
        UINT height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT mip_count = 1;
        bool immutable_values = false;
        bool operator==(const NativeTextureArrayKey& other) const noexcept {
            return width == other.width && height == other.height && format == other.format &&
                mip_count == other.mip_count && immutable_values == other.immutable_values;
        }
    };
    struct NativeTextureArrayKeyHash {
        std::size_t operator()(const NativeTextureArrayKey& key) const noexcept;
    };
    struct NativeTextureArraySlot { UINT slice = 0; std::uint64_t generation = 0; };
    struct NativeTextureArrayPage {
        UINT capacity = 0;
        UINT used = 0; // live slots
        // Slices handed out. A dropped slot's slice is not handed out again
        // while the page lives (a persistent depth record may still sample
        // it); the page goes with its last live slot.
        UINT next = 0;
        std::uint64_t last_used = 0;
        std::uint64_t last_used_frame = 0; // native_frame_index_ (VRAM budget idle test)
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        std::unordered_map<std::uint64_t, NativeTextureArraySlot> slots;
    };
    struct NativeMeshKey {
        NativeMeshCacheSelector selector{};
        std::string resource_key;
        std::uint32_t world_part = 0;
        bool static_lods = false;
        std::uint32_t shape_count = 1;
        // Object-space shape-resolved meshes: the exact shape (float bits).
        std::array<std::uint32_t, 3> shape_bits{};
        std::uint32_t position_groups = 0;
        std::uint32_t normal_groups = 0;
        std::uint32_t component_count = 0;
        std::uint32_t texture_coordinate_count = 0;
        std::uint32_t index_count = 0;
        std::uint32_t decoded_channels = 0;
        bool operator==(const NativeMeshKey& other) const {
            return selector == other.selector && resource_key == other.resource_key &&
                world_part == other.world_part && static_lods == other.static_lods &&
                shape_count == other.shape_count && shape_bits == other.shape_bits &&
                position_groups == other.position_groups && normal_groups == other.normal_groups &&
                component_count == other.component_count &&
                texture_coordinate_count == other.texture_coordinate_count &&
                index_count == other.index_count &&
                decoded_channels == other.decoded_channels;
        }
    };
    struct NativeMeshKeyHash {
        std::size_t operator()(const NativeMeshKey& key) const noexcept;
    };
    struct NativeGeometrySpan {
        UINT page = UINT_MAX;
        UINT offset = 0;
        UINT bytes = 0;
    };
    struct NativeGeometryFreeSpan { UINT offset = 0, bytes = 0; };
    struct NativeGeometryPage {
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> srv;
        D3D12BufferRef d3d12;  // x64 host, renderer_backend=d3d12: the page instead of buffer/srv
        UINT capacity = 0;
        UINT high_water = 16;
        std::vector<NativeGeometryFreeSpan> free;
        // A page a worker built for one neighbour (NativePrebuiltPage): never
        // used for general allocation; released when its last mesh goes.
        bool dedicated = false;
        std::uint64_t live = 0;
    };
    struct NativeMesh {
        NativeGeometrySpan allocation{};
        UINT position_offset = 0;
        UINT normal_offset = 0;
        UINT component_offset = 0;
        UINT texture_coordinate_offset = 0;
        UINT diffuse_storage_offset = UINT_MAX;
        UINT specular_storage_offset = UINT_MAX;
        UINT diffuse_offset = UINT_MAX;
        UINT specular_offset = UINT_MAX;
        UINT terrain_vertex_offset = UINT_MAX;
        // Terrain: any liquid-surface vertex, any other vertex (a liquid
        // batch draws its two parts with their own pixel shaders).
        bool has_liquid = false, has_ground = true;
        // Its liquid vertices by type: 1 water (not self-illuminated), 2 lava
        // (self-illuminated), 4 a swamp share (flags bits 29-31); the flush
        // picks the liquid program of the batch's types.
        std::uint8_t liquid_types = 0;
        // Terrain: first triangle of the steep tile class (decode order: flat
        // tiles, then steep ones; the triangle count when none is steep);
        // UINT_MAX when the mesh's tiles are not classified.
        UINT steep_first_triangle = UINT_MAX;
        UINT index_offset = 0;
        // Static LOD (static_lod.h): reduced levels after the full index
        // list, with a 64-byte table just before it (nativeStaticLodSelect);
        // a record drawing them reserves the first level's triangles more
        // (both levels of the crossfade band).
        UINT lod_levels = 0, lod_extra_triangles = 0;
        // Object-space mesh: its bounding sphere precedes the components.
        bool sphere_block = false;
        float bounds_min[3]{};
        float bounds_max[3]{};
        std::array<std::uint32_t, 5> geometry_revision{};
        bool geometry_revision_valid = false;
        std::array<std::uint32_t, 3> color_revision{};
        bool color_revision_valid = false;
        std::uint64_t bytes = 0;
        std::uint64_t last_used_frame = 0;
        std::uint32_t pending_references = 0;
        std::uint32_t owned_references = 0;
        // Set by a stale/dedicated retirement pass between release and erase.
        bool retiring = false;
    };
    struct NativeInstanceGpu {
        float astral[4]{};
        D3DMATRIX object_to_clip{};
        D3DMATRIX object_to_world{};
        std::array<UINT, 4> geometry_offsets{};
        std::array<UINT, 2> color_offsets{UINT_MAX, UINT_MAX};
        float uv_shift[2]{};
        UINT flags = 0;
        UINT default_diffuse = 0xffffffffu;
        UINT default_specular = 0xffffffffu;
        UINT morph_offset = 0;
        UINT morph_count = 0;
        UINT lighting_enabled = 0;
        UINT padding[4]{};
        UINT terrain_vertex_offset = UINT_MAX;
        UINT terrain_phase_offset = 0;
        UINT terrain_phase_count = 0;
        UINT terrain_tiles_per_side = 0;
        UINT terrain_flags = 0;
        float terrain_wind_direction[2]{};
        float terrain_wind_force = 0.0f;
        float terrain_wind_force_normalized = 0.0f;
        float terrain_wind_phase = 0.0f;
        float terrain_precise_time = 0.0f;
        float terrain_fog_min[2]{};
        float terrain_fog_max[2]{};
        float terrain_fog_distance = 0.0f;
        UINT terrain_fog_enabled = 0;
        float emissive[4]{};
        float specular[4]{};
        float material_alpha = 1.0f;
        float material_diffuse[3]{1.0f, 1.0f, 1.0f};
        UINT world_geometry = 0;
        UINT shape_count = 1;
        UINT position_groups = 0;
        UINT second_morph_offset = 0;
        float morph_fraction = 0;
        float shape[3]{};
        UINT pose_node = UINT_MAX;
        UINT skn_maps[4]{};
        float skn_factors[4]{};
        float skn_emission[4]{};
        float skn_base[4]{};
    };
    struct NativeDrawGpu {
        UINT instance_index = 0;
        UINT component_offset = 0;
        UINT index_offset = 0;
        UINT index_count = 0;
        UINT output_triangle_base = 0;
        UINT indirect_args_byte_offset = 0;
        UINT ordered_triangle_offset = 0;
        UINT preserves_order = 0;
        float bounds_min[4]{};
        float bounds_max[4]{};
        UINT prepared_vertex_base = 0;
        UINT component_count = 0;
        UINT prepare_world = 0;
        UINT color_visible = 1;
        UINT world_state = UINT_MAX, world_generation_low = 0, world_generation_high = 0;
        float geometry_radius = 0;
        // Terrain tile classes: triangles from here on are steep-class ones
        // (NativeMesh::steep_first_triangle; UINT_MAX: none).
        UINT steep_first_triangle = UINT_MAX;
    };
    struct NativeDrawWorkGpu {
        UINT record = 0, output_triangle_base = 0, indirect_args_byte_offset = 0;
        UINT ordered_triangle_offset = 0, flags = 0, prepared_vertex_base = 0;
        UINT work_count = 0;
        // A split batch's steep region: its first output triangle and its own
        // indirect-args slot (UINT_MAX: the batch is not split; steep
        // triangles then stay in the batch's one region).
        UINT steep_output_triangle_base = 0, steep_args_byte_offset = UINT_MAX;
    };
    struct NativePreparedVertexGpu {
        float position[4]{};
        float diffuse[4]{};
        float specular[4]{};
        float texture_coordinate[2]{};
        UINT texture_reference[2]{};
    };
    struct NativeWorldVertexGpu {
        float position[4]{};
        float normal[4]{};
        float texture_coordinate[2]{};
        float undeformed_xy[2]{};
        UINT diffuse = 0xffffffffu;
        UINT specular = 0xff000000u;
        UINT source_position = 0;
        UINT padding = 0;
        float tangent[4]{};
    };
    static_assert(sizeof(NativeInstanceGpu) == 432,
                  "NativeInstanceGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativeWorldLighting) == 48,
                  "NativeWorldLighting must match the HLSL constant-buffer layout");
    static_assert(sizeof(NativeDrawGpu) == 100,
                  "NativeDrawGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativeDrawWorkGpu) == 36,
                  "NativeDrawWorkGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativePreparedVertexGpu) == 64,
                  "NativePreparedVertexGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativeWorldVertexGpu) == 80,
                  "NativeWorldVertexGpu must match the HLSL structured-buffer layout");
    struct NativeMaterialKey {
        // Only state consumed by the current native D3D11 pipeline belongs in
        // this key. Legacy object identity and dormant D3D7 slots must not
        // fragment otherwise identical GPU work.
        std::array<std::uint32_t, 30> state{};
        std::uint32_t shadow = 0, contact_map = 0;
        std::uint64_t texture_backing = 0;
        std::uint64_t skn_material = 0;
        std::uint64_t texture_generation = 0;
        bool operator==(const NativeMaterialKey& other) const noexcept {
            return skn_material == other.skn_material && state == other.state && shadow == other.shadow && contact_map == other.contact_map && texture_backing == other.texture_backing &&
                texture_generation == other.texture_generation;
        }
    };
    struct NativeMaterialKeyHash {
        std::size_t operator()(const NativeMaterialKey& key) const noexcept;
    };
    struct NativePreparedMeshSlot {
        struct DeformationKey {
            std::array<float, 24> values{};
            UINT wave_flags = 0;
            UINT pose_slot = UINT_MAX;
            UINT pose_allocation = UINT_MAX;
            std::uint64_t layer_shift_revision = 0; // terrain: shader-applied layer shifts
            bool operator==(const DeformationKey& other) const noexcept {
                return values == other.values &&
                    layer_shift_revision == other.layer_shift_revision &&
                    wave_flags == other.wave_flags &&
                    pose_slot == other.pose_slot && pose_allocation == other.pose_allocation;
            }
        };
        NativeMesh* mesh = nullptr;
        NativePreparedIdentity identity{};
        std::array<std::uint32_t, 5> geometry_revision{};
        std::array<std::uint32_t, 3> color_revision{};
        std::uint32_t position_groups = 0;
        std::uint32_t normal_groups = 0;
        std::uint32_t component_count = 0;
        std::uint32_t texture_coordinate_count = 0;
        std::uint32_t index_count = 0;
        std::uint32_t decoded_channels = 0;
        std::uint32_t generation = 1;
        std::uint64_t last_used_frame = 0;
        bool geometry_revision_valid = false;
        bool color_revision_valid = false;
        bool active = false;
        std::weak_ptr<const void> owner;
        std::uint64_t source_revision = 0;
        DeformationKey world_key{};
        std::string resource_key;
        std::uint32_t world_part = 0;
        UINT shape_count = 1;
        UINT world_base = UINT_MAX, world_count = 0;
        UINT world_allocation = UINT_MAX;
        std::uint64_t world_generation = 0;
        bool world_valid = false;
    };
    // The source slot owns a range; queued consumers pin its exact allocation.
    // Retired ranges can be reused only after the last queued reference ends.
    struct NativeGpuRange {
        UINT base = 0, count = 0, references = 0;
        bool owned = false;
        std::uint64_t generation = 1;
        UINT owners = 0;
        std::uint64_t preparation_epoch = 0;
        UINT preparation_work = 0;
    };
    enum class NativeRangeBuffer { world, pose, animation };
    // Immutable animation bytes and current pose allocations belong to the
    // existing native resource owner. Slots carry no game membership of their own.
    struct NativeSkeletonAsset {
        UINT offset = 0, bytes = 0, allocation = UINT_MAX;
        std::weak_ptr<const ModelCpuAsset> model;
        std::weak_ptr<const AnimationClipAsset> clip;
        bool animated = false;
    };
    struct NativePoseGpu {
        UINT asset = 0, output = 0, frame = 0;
        float fraction = 0;
        float shape[3]{};
        float height_scale = 1;
        float center_shift[3]{};
        UINT padding = 0;
        std::array<float,16> root_to_world{};
        std::array<UINT,2> generation{};
    };
    struct NativePoseNodeGpu {
        std::array<float,4> rotation{}, translation{};
        std::array<float,4> rows[3]{};
        UINT morph_first = 0, morph_second = 0, morph_count = 0;
        float morph_fraction = 0;
        std::array<UINT,2> generation{};
    };
    struct NativeWorldStateGpu {
        std::array<UINT,2> geometry{},pose{};
        UINT skn_lod = 0;
        UINT preparations = 0,components = 0; // test seam; production never increments these
    };
    static_assert(sizeof(NativePoseGpu)==120 && sizeof(NativePoseNodeGpu)==104 && sizeof(NativeWorldStateGpu)==28,
        "Pose records must match the structured HLSL layout");
    struct NativePoseSlot {
        NativePreparedIdentity identity{};
        std::weak_ptr<const void> owner;
        std::shared_ptr<const VisualSkeletonBinding> binding;
        NativePoseGpu value{};
        UINT generation = 1, count = 0, allocation = UINT_MAX;
        UINT animation_allocation = UINT_MAX;
        std::uint64_t revision = 0, scheduled = 0;
        bool active = false, valid = false;
    };
    struct NativePoseWork {
        NativePoseGpu value{};
        UINT slot = UINT_MAX, count = 0, allocation = UINT_MAX;
        std::uint64_t revision = 0, allocation_generation = 0;
    };
    struct NativePreparedMaterialSlot {
        NativePreparedIdentity identity{};
        NativeMaterialKey material_key{};
        std::uint64_t state_key = 0;
        std::uint64_t queue_epoch = 0;
        std::uint64_t presentation_serial = 0;
        std::uint32_t state_bucket = UINT_MAX;
        std::uint32_t generation = 1;
        std::uint64_t last_used_frame = 0;
        NativeOpacityClass opacity = NativeOpacityClass::ordered;
        bool reorderable = false;
        bool preserve_order = false;
        bool active = false;
        std::weak_ptr<const void> owner;
        bool persistent = false;
        std::array<UINT,2> persistent_instance{UINT_MAX,UINT_MAX};
    };
    struct NativeStateBucket {
        FixedState state{};
        ComPtr<ID3D11ShaderResourceView> texture_srv;
        D3D12TextureRef texture_d3d12;  // renderer_backend=d3d12: the texture instead of texture_srv
        std::array<ComPtr<ID3D11ShaderResourceView>,3> skn_textures;
        NativeMaterialKey material_key{};
        SemanticKind semantic_kind = SemanticKind::unknown;
        NativeOpacityClass opacity = NativeOpacityClass::ordered;
        std::uint64_t state_key = 0;
        bool texture_array = false;
        bool occupied = false;
        UINT references = 0;
        std::vector<std::size_t> selected;
        std::uint64_t selection_epoch = 0;
    };
    struct NativeOwnedTexture {
        std::uint64_t backing = 0;
        std::uint64_t generation = 0;
        std::uint64_t surface_id = 0;
        bool camp_surface_alias = false;
        // Re-entered from a detached static's retained copy: its client
        // surface is gone, so the world sweep retires it like an owned value.
        bool detached_copy = false;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        DWORD source_fourcc = 0;
        UINT width = 0;
        UINT height = 0;
        UINT mip_count = 0;
        std::uint64_t last_used = 0;
        std::uint64_t last_used_frame = 0; // native_frame_index_ (VRAM budget idle test)
        UINT budget_skip = 0; // top mips left out under VRAM budget pressure
        std::uint64_t shared_floor = 0; // Last neighbour load that counted this reuse.
        // `texture` is the client surface's own texture (one texture per
        // image): its bytes are the surface's, and dropping this entry frees
        // nothing. Cleared when the surface moves to another texture.
        bool surface_shared = false;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        D3D12TextureRef d3d12;  // renderer_backend=d3d12: the texture instead of texture/srv
    };
    struct NativeQueuedDraw {
        NativeMesh* mesh = nullptr;
        UINT geometry_page = UINT_MAX;
        NativeInstanceGpu instance{};
        float bounds_min[3]{};
        float bounds_max[3]{};
        UINT index_count = 0;
        UINT component_count = 0;
        UINT state_bucket = 0;
        widescreen::HudComposition hud_composition{};
        camp::Clip camp_clip;
        bool reorderable = false;
        bool preserve_order = false;
        UINT world_slot = UINT_MAX, world_base = 0, world_allocation = UINT_MAX;
        float geometry_radius = 0;
        std::uint64_t world_generation = 0;
        bool prepare_world = false;
        UINT pose_slot = UINT_MAX, pose_allocation = UINT_MAX;
        UINT animation_allocation = UINT_MAX;
        UINT persistent_instance = UINT_MAX;
        // D3D12: the texture instance.padding[0] names by bindless index, kept
        // alive by the command (and the record) that stores the index.
        D3D12TextureRef texture12;
        std::array<D3D12TextureRef,3> skn_textures12;
        // D3D12: the texture value (backing) instance.padding[1]'s material profile index belongs to.
        std::uint64_t material_backing = 0;
    };
    struct NativeInstanceRecord {
        NativeQueuedDraw command{};
        std::shared_ptr<const NativeLodIdentity> lod_observer;
        NativeDrawGpu descriptor{};
        std::weak_ptr<const void> owner;
        UINT references = 0;
        bool occupied = false, owned = true, uploaded = false, descriptor_uploaded = false, scheduled = false, group_bound = false;
        bool depth_current = false;
        std::uint64_t source_revision = 0, values_revision = 0;
        NativeShadowCasterRevision depth_revision;
        std::uint64_t depth_world_generation = 0;
        std::uint64_t depth_pose_revision = 0, depth_animation_generation = 0;
        UINT depth_pose_generation = 0;
        std::uint64_t wound_id = 0, wound_generation = 0;
        std::uint64_t terrain_texture_id = 0,terrain_texture_generation = 0;
        bool terrain_texture_owned = false;
        std::array<UINT,3> inherited_state{};
        // Fur prototype (renderer_fur.cpp): this frame's colour submission's
        // fur profile and its records' node base (NativePreparedPartSubmission).
        std::shared_ptr<const FurProfile> fur;
        UINT fur_node_base = 0;
    };
    struct NativeStaticShadowRecord {
        NativeShadowCasterRevision revision{};
        UINT instance = UINT_MAX;
        UINT mesh_slot = UINT_MAX, mesh_generation = 0;
        UINT material_slot = UINT_MAX, material_generation = 0;
        std::uint64_t seen = 0;
    };
    struct NativeSelectedRecord {
        UINT record = UINT_MAX, passes = 0; // bit 0 color; bits 8..11 light slices
        // Interface semantics (an owned HUD mesh, a preview): never drawn in a world flush.
        bool interface_record = false;
    };
    // Only native world-map arrows, owned values retained until this same
    // map Draw ends. The ordinary native queue keeps all other records.
    struct MapPreviewMarker {
        NativeSelectedRecord selected;
        std::vector<std::uint8_t> morphs;
        NativeWorldLighting lighting{};
        bool lighting_valid = false;
    };
    std::vector<MapPreviewMarker> map_preview_markers_;
    bool map_preview_marker_collecting_ = false;
    void discard_world_map_markers() noexcept;
    struct NativeInstanceUpdate {
        UINT destination = 0, count = 0, flags = 0;
        NativeInstanceGpu value{};
        NativeDrawGpu descriptor{};
    };
    static_assert(sizeof(NativeInstanceUpdate)==544,"NativeInstanceUpdate must match the structured HLSL layout");
    struct NativeBatch {
        std::size_t first_draw = 0;
        std::size_t draw_count = 0;
        UINT state_bucket = 0;
        UINT geometry_page = UINT_MAX;
        UINT output_triangle_base = 0;
        // Terrain tile classes: a batch whose draws hold steep-class triangles
        // draws them from their own region and arguments slot with the steep
        // pixel variant (UINT_MAX: not split); its first slot keeps the flat ones.
        UINT steep_args_slot = UINT_MAX, steep_triangle_base = 0;
        // Every draw's tiles are classified: the first region (all of the
        // batch when not split) holds no steep-class triangle and draws with
        // the flat terrain pixel variant.
        bool flat_pixels = false;
        // Some draw can reach the ground contact band (native_draw_may_contact);
        // without one the batch draws the variant with the contact blend
        // compiled out.
        bool contact = true;
        // Some draw is posed (skinned figure, animated object): kept out of
        // the depth prepass.
        bool posed = false;
        bool preserves_order = false;
        bool foliage = false;
        std::array<std::uint64_t,6> slice_triangles{};
        std::array<UINT,6> slice_records{};
    };
    struct NativeDispatchRange {
        UINT first = 0, count = 0;
        UINT geometry_page = UINT_MAX;
        UINT preparations = 0;
    };
    struct NativeSubmissionLayout {
        std::vector<NativeDrawWorkGpu> work;
        std::vector<std::size_t> work_draws;
        std::vector<std::size_t> source_draws;
        std::vector<UINT> source_records;
        std::vector<NativeDispatchRange> dispatches;
        std::vector<NativeDispatchRange> geometry_dispatches;
        std::vector<NativeBatch> batches;
        std::vector<UINT> arguments;
        std::uint64_t source_revision = 0;
        std::uint64_t static_revision = 0;   // static-set layout only
        std::uint64_t contact_generation = 0; // native_contact_generation_ its batches' contact bits used
        UINT triangles = 0, components = 0;
        // Static-set layouts (slots 2..4): their own uploaded draw work and
        // the per-batch totals the draw loop reads, valid for exactly one set
        // revision and static content revision (0: nothing to reuse).
        struct StaticBake {
            struct Batch {
                UINT first_record = 0, lit_draws = 0;
                std::uint64_t triangles = 0;
                float xy_min[2]{}, xy_max[2]{};   // world XY range of the draws (terrain material cache)
                bool liquid = false, ground = false, unlit = true;
                std::uint8_t liquid_types = 0;
                bool neighbour = false; // a neighbour map's draw (native_neighbour_flag)
            };
            std::vector<Batch> batches;
            std::vector<NativeMesh*> meshes;   // adjacent duplicates folded
            std::uint64_t prepared_vertices = 0, logical_draws = 0;
            std::uint64_t opaque_draws = 0, cutout_draws = 0, material_buckets = 0;
            std::uint64_t reorderable_draws = 0, ordered_draws = 0;
            std::uint64_t set_revision = 0, content_revision = 0;
            ComPtr<ID3D11Buffer> work;
            ComPtr<ID3D11ShaderResourceView> work_srv;
            UINT work_capacity = 0;
            D3D12BufferRef work12;  // D3D12 backend: the same work list
        } static_bake;
    };
    HRESULT create_targets();
    DebugResourceViewAvailability debug_resource_view_availability() const noexcept;
    DebugShadowInfo debug_shadow_info() const noexcept;
    HRESULT render_debug_resource_view(DebugResourceView view,
                                       DebugOverlayMode mode) noexcept;
    HRESULT ensure_debug_texture_pipeline() noexcept;
    HRESULT ensure_debug_stencil_view() noexcept;
    void remember_debug_projection(const FixedState& state,
                                   SemanticKind semantic_kind) noexcept;
    HRESULT render_debug_texture(DebugResourceView view) noexcept;
    HRESULT render_debug_texture_d3d12(DebugResourceView view) noexcept;
    HRESULT create_pipeline();
    // Creates the shader object of every program the current settings use and
    // this process has not prepared yet, on worker threads (from the embedded
    // bytecode; a missing program is compiled), presenting a progress window
    // while that takes more than 150 ms. Called before the pipelines are
    // created and after every present (settings may have changed).
    void prepare_shaders() noexcept;
    void register_embedded_shaders() noexcept;
    // Shader crash breadcrumb (eid3d11_shader_crash.txt beside the ini): a
    // preparation names the last program group it handed to the driver until
    // the next completed present closes it. Still there at startup, the last
    // run died inside the driver with that group. After a parallel
    // preparation, recover switches preparation to one worker for good;
    // after a serial one it turns the group's feature off (or the nearest
    // earlier optional one). Either way it logs and owes a notice.
    void recover_shader_crash() noexcept;
    void close_shader_marker() noexcept;
    HRESULT load_camp_art();
    HRESULT create_clear_pipeline();
    HRESULT draw_screen_rectangle(const ScreenRectangleRequest& request,
                                  const D3DVIEWPORT7& viewport,float grid_step=0.f);
    HRESULT clear_rectangles(DWORD rectangle_count, const D3DRECT* rectangles,
                             DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT clear_target_rectangles(const D3D11_RECT* rectangles, DWORD rectangle_count,
                                    DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT create_native_pipeline();
    HRESULT render_pending_blits();
    // captured: the frames copied on the game thread (render_thread on);
    // nullptr reads the live panels here (render_thread off, game thread).
    HRESULT draw_hud_asset_frames(const std::vector<HudAssetFrame>* captured);
    std::shared_ptr<const TextureCpuAsset> hud_frame_texture_;
    bool hud_frame_texture_failed_=false;
    std::uint64_t hud_frames_frame_=UINT64_MAX;
    HRESULT get_native_texture_array_slice(SurfaceImage& image,
                                           ComPtr<ID3D11ShaderResourceView>& srv,
                                           UINT& slice,
                                           const TerrainTexturePage* authored_page=nullptr,
                                           UINT demand_slices=0);
    HRESULT get_native_owned_texture(
        const NativeOwnedTextureView& source,
        ComPtr<ID3D11ShaderResourceView>& srv, std::uint64_t& backing, NativeGeometryResult* result, bool world_mips = false,
        bool budget_reducible = false);
    HRESULT reserve_native_owned_texture_bytes(std::uint64_t bytes,bool arrays);
    HRESULT retire_native_owned_texture_bytes(std::uint64_t bytes,bool arrays);
    std::uint64_t native_owned_texture_bytes(bool arrays) const;
    // Retainer for detached statics: the resident cache copy of a surface generation.
    static std::shared_ptr<ID3D11Texture2D> retain_detached_surface_texture(
        void* context, const OwnedVisualSurface& surface) noexcept;
    std::uint64_t native_shadow_fixed_bytes() const;
    HRESULT get_native_mesh(const NativeGeometryView& view, NativeMesh*& mesh);
    HRESULT get_native_prepared_mesh(const NativeGeometryView& view,
                                     NativeMesh*& mesh);
    HRESULT resolve_native_prepared_material(
        const NativeGeometryView& view, const FixedState& state,
        SurfaceImage* texture, ID3D11ShaderResourceView* texture_srv,
        bool texture_array, NativeOpacityClass opacity, bool reorderable,
        const NativeMaterialKey& material_key, std::uint64_t state_key,
        UINT& state_bucket, const D3D12TextureRef* d3d12_texture = nullptr);
    HRESULT get_native_material_group(const NativeMaterialKey& key,const FixedState& state,
        ID3D11ShaderResourceView* texture,bool texture_array,SemanticKind semantic,
        NativeOpacityClass opacity,std::uint64_t state_key,UINT& group,
        const D3D12TextureRef* d3d12_texture = nullptr);
    void bind_native_material_group(NativeInstanceRecord& record,UINT group) noexcept;
    void trim_native_material_groups() noexcept;
    void order_native_groups(bool shadows);
    EmbeddedArenaCache embedded_arena_cache_;
    EmbeddedGolemCache embedded_golem_cache_;
    EmbeddedCurseCache embedded_curse_cache_;
    HRESULT queue_native_impl(const NativeGeometryView& view,
                              const FixedState& state, SurfaceImage* texture,
                              NativeGeometryResult* result, bool prepared,
                              ID3D11ShaderResourceView* direct_texture = nullptr,
                              std::uint64_t direct_texture_identity = 0,
                              std::uint64_t direct_texture_generation = 0,
                              bool direct_array = false, UINT direct_slice = 0,
                              UINT* registered = nullptr,
                              const D3D12TextureRef* d3d12_texture = nullptr);
    HRESULT queue_native_owned_impl(const NativeGeometryView& view,const FixedState& state,
                                    NativeGeometryResult* result,UINT* registered);
    // Invalidates every active slot of the meshes marked retiring in one slot
    // pass, then erases those meshes.
    void erase_retiring_native_meshes() noexcept;
    void invalidate_native_prepared_handles() noexcept;
    HRESULT get_native_skeleton_asset(const std::shared_ptr<const VisualSkeletonBinding>& binding,
                                     UINT& offset,UINT& allocation);
    HRESULT ensure_native_animation_buffer(UINT required);
    HRESULT resolve_native_terrain_phases(const NativeGeometryView& view,NativeQueuedDraw& draw);
    HRESULT queue_native_terrain(const NativeGeometryView& view,const FixedState& state,
                                 SurfaceImage* texture,NativeGeometryResult* result);
    static void native_terrain_values(NativeInstanceGpu& value,const NativeTerrainState& terrain,
                                      bool full_sun_comparison) noexcept;
    HRESULT upload_native_terrain_phases();
    void discard_native_terrain_phase_work() noexcept;
    HRESULT resolve_native_pose(NativeWorldGeometry::Pose* pose,const NativePreparedIdentity& identity,
                                UINT node,NativeQueuedDraw& draw);
    HRESULT ensure_native_pose_nodes(UINT required);
    HRESULT prepare_native_poses();
    void retire_native_poses() noexcept;
    HRESULT resolve_native_instance(const NativeGeometryView& view,NativeQueuedDraw& draw);
    HRESULT store_native_instance(UINT& current,const std::weak_ptr<const void>& owner,NativeQueuedDraw& draw);
    void enqueue_native_record(NativeSelectedRecord selected);
    NativeMaterialKey native_material_key(const FixedState& state,SemanticKind kind,
        NativeOpacityClass opacity,bool lighting,bool textured,bool texture_array,
        std::uint64_t backing,std::uint64_t generation,UINT contact_map=0) const noexcept;
    static std::uint64_t native_material_state_key(const NativeMaterialKey& key) noexcept;
    UINT allocate_native_instance_record();
    NativeQueuedDraw& native_queued_draw(std::size_t index) noexcept {
        return native_instance_records_[native_pending_draws_[index].record].command;
    }
    const NativeQueuedDraw& native_queued_draw(std::size_t index) const noexcept {
        return native_instance_records_[native_pending_draws_[index].record].command;
    }
    HRESULT upload_native_instances();
    bool set_native_world_clip(const std::array<float,16>& clip);
    void release_native_instance(UINT index) noexcept;
    void retire_native_material_instances(NativePreparedMaterialSlot& slot) noexcept;
    NativeDrawGpu native_draw_descriptor(const NativeQueuedDraw& draw,UINT index) const noexcept;
    HRESULT resolve_native_world_range(const NativeGeometryView& view,
                                       NativeQueuedDraw& draw);
    HRESULT ensure_native_world_buffer(UINT required);
    HRESULT create_native_world_resource(UINT capacity, ComPtr<ID3D11Buffer>& buffer,
        ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11UnorderedAccessView>& uav);
    HRESULT reclaim_native_world_ranges();
    HRESULT ensure_native_world_states();
    void native_world_draw_values(const NativeQueuedDraw& source,NativeDrawGpu& target) const noexcept;
    void begin_native_preparations() noexcept;
    HRESULT prepare_native_submission_layout(bool shadows,unsigned slot);
    void refresh_native_draw_work(NativeSubmissionLayout& layout,bool shadows);
    void native_layout_changed() noexcept;
    HRESULT allocate_native_range(std::vector<NativeGpuRange>& ranges,UINT count,
                                  NativeRangeBuffer buffer,UINT& allocation);
    // Every place a range loses its owner or its last queued reference calls
    // this; a range that is now free joins its buffer's best-fit index.
    void native_range_freed(std::vector<NativeGpuRange>& ranges,UINT index) noexcept;
    void retire_native_world_range(NativePreparedMeshSlot& slot) noexcept;
    void retire_native_source_values() noexcept;
    HRESULT create_native_shadow_resources();
    // Sun-shadow early-out cells (after create_native_shadow_resources).
    HRESULT build_native_shadow_range(std::uint8_t slices) noexcept;
    void ensure_native_shadow_range() noexcept;
    void release_native_shadow_range() noexcept;
    HRESULT create_shadow_history_targets();
    bool prepare_shadow_history() noexcept;
    void commit_shadow_history() noexcept;
    HRESULT grow_native_point_shadow_page(std::size_t page,UINT cubes,bool overlay);
    std::uint64_t native_point_shadow_bytes() const noexcept;
    void point_shadow_capacity_failure(const char* reason,HRESULT hr) noexcept;
    HRESULT flush_native_shadows();
    // D3D12 backend (renderer_native_d3d12.cpp): the sun maps and their static
    // layer as executor depth arrays, this frame's shadow constants (b2) and
    // the same flush through the executor.
    struct ShadowD3D12 {
        D3D12TextureRef maps, statics, range_cells, range;
        NativeShadowConstants constants{}, point_constants{};
    } shadow12_;
    HRESULT flush_native_shadows_d3d12();
    // The world image to the output on D3D12 (SceneComposite::render: SMAA or
    // the copy) and the post passes' own resources.
    struct PostD3D12 {
        std::vector<ShaderProgram> programs;  // SceneComposite::shader_programs order
        D3D12TextureRef area, search, edges, weights;
        struct {  // depth_of_field_d3d12 (DepthOfFieldPass's state)
            std::vector<ShaderProgram> programs;
            D3D12TextureRef work[2], focus[2];
            UINT width = 0, height = 0, work_width = 0, work_height = 0, divisor = 0, current_focus = 0;
            bool history_valid = false;
            double last_seconds = 0;
        } dof;
        struct {  // sun_rays_d3d12
            std::vector<ShaderProgram> programs;
            D3D12TextureRef mask, blur;
            UINT width = 0, height = 0;
        } rays;
        struct {  // bloom_d3d12
            std::vector<ShaderProgram> programs;
            D3D12TextureRef mips[BloomPass::kMaxMips];
            UINT width = 0, height = 0, count = 0;
            int quality = 0;
            // The glow of blended world particles (draw_game_effect_d3d12, the
            // D3D11 glow_ target): valid for bloom only in glow_frame.
            D3D12TextureRef glow;
            std::uint64_t glow_frame = UINT64_MAX;
        } bloom;
    } post12_;
    // The temporal upscalers on D3D12 (DLSS, FSR 2; the shared frame logic
    // is begin/finish_upscaler_frame): the render-size resolved image the
    // composite and post passes write (SceneComposite's resolved_), the
    // camera motion and the object motion targets (SceneComposite's), and
    // the output-size image the upscaler writes.
    struct UpscalerD3D12 {
        D3D12TextureRef resolved, motion, object_motion, object_depth, output;
        // Object motion: last frame's world vertices (copied at each world
        // frame's start) and nativeMotionVS/PS's b11 values of this frame.
        D3D12BufferRef previous_world;
        std::array<float,20> motion_constants{};
        std::uint64_t motion_constants_frame = UINT64_MAX;
        Fsr2UpscalerD3D12 fsr;
        UpscaleSharpenD3D12 sharpen;  // upscale_sharpness after DLSS
    } upscale12_;
    HRESULT ensure_upscaler_d3d12();
    void apply_upscaler_mode_d3d12();
    void begin_object_motion_d3d12() noexcept;
    bool object_motion_ready_d3d12() noexcept;
    bool draw_object_motion_d3d12(const ShaderProgram& vertex,const ShaderProgram& pixel,const D3D12NativeState& batch_state,
                                  const RECT* scissor,UINT args_offset);
    void camera_motion_d3d12(const float clip_to_world[16],const float previous[16]);
    void present_upscaled_d3d12();
    HRESULT upscale_world_d3d12();
    HRESULT filtered_upscale_d3d12();
    bool render_dlss_d3d12();
    bool render_fsr_d3d12();
    // Where the composite and post passes write: the resolved image while a
    // temporal upscaler runs or the render scale is below 100%, else the back
    // buffer (null).
    D3D12Texture* scene_output_d3d12() const noexcept {
        return (temporal_upscaler() || width_ != output_width_ || height_ != output_height_) && upscale12_.resolved
            ? upscale12_.resolved.get() : nullptr;
    }
    HRESULT resolve_world_d3d12(bool smaa);
    HRESULT depth_of_field_d3d12(int quality,float strength,const float clip_to_world[16],double seconds);
    HRESULT sun_rays_d3d12();
    HRESULT bloom_d3d12(int quality,float strength,float ceiling,float threshold,bool contribution_only);
    HRESULT upload_native_shadow_constants(UINT slice);
    void retire_native_shadows() noexcept;
    bool reject_native_contribution(const NativeGeometryView& view,
                                    NativeGeometryResult* result) noexcept;
    void release_native_queued_draw(UINT record,bool mesh_pending=true) noexcept;
    void discard_native_unqueued_work() noexcept;
    void advance_native_queue_epoch() noexcept;
    HRESULT append_native_dynamic(const NativeGeometryView& view,
                                  UINT& morph_offset, UINT& terrain_phase_offset);
    HRESULT ensure_native_stream_buffers(UINT dynamic_bytes,
                                         UINT draw_count, UINT triangle_count,
                                         UINT component_count,
                                         UINT batch_count);
    HRESULT create_native_geometry_arena();
    HRESULT ensure_native_geometry_capacity(UINT required);
    HRESULT ensure_native_geometry_page_capacity(UINT page, UINT required);
    HRESULT allocate_native_geometry_span(UINT bytes, NativeGeometrySpan& span);
    bool release_native_geometry_span(const NativeGeometrySpan& span) noexcept;
    UINT native_geometry_page_limit() const noexcept;
    std::uint64_t native_geometry_capacity_bytes() const noexcept;
    std::uint64_t native_geometry_high_water_bytes() const noexcept;
    std::uint64_t native_geometry_free_bytes() const noexcept;
    HRESULT create_native_geometry_resource(
        UINT capacity, ComPtr<ID3D11Buffer>& buffer,
        ComPtr<ID3D11ShaderResourceView>& srv);
    HRESULT upload_native_geometry(UINT page, UINT offset,
                                   const void* data, UINT bytes);
    ID3D11ShaderResourceView* native_geometry_page_srv(UINT page) const noexcept;
    void refresh_native_mesh_records(NativeMesh& mesh) noexcept;
    HRESULT upload_native_work_range(const NativeDispatchRange& range, bool capture_lod = false);
    HRESULT trim_native_mesh_cache();
    // Retires unowned geometry last used before frame `before`.
    HRESULT retire_stale_native_geometry(std::uint64_t before);
    // Releases the buffers of general geometry pages that hold no live span.
    void release_empty_native_geometry_pages() noexcept;
    // Owned textures and immutable array slots not used since the world
    // began (queue epoch `since`); client-surface entries go with their surface.
    void retire_world_textures(std::uint64_t since) noexcept;
    // Drops the array slots keyed `id` (immutable: an owned texture's backing;
    // else a surface id) and releases pages left without a live slot.
    void drop_native_texture_array_slots(std::uint64_t id, bool immutable) noexcept;
    HRESULT draw_fixed_function(D3DPRIMITIVETYPE type, std::vector<FixedFunctionVertex>& vertices,
        const WORD* indices, DWORD index_count, const FixedState& state, SurfaceImage* texture,
        SemanticKind semantic_kind, std::uint32_t caller_rva, std::uint32_t legacy_draw_count,
        const widescreen::HudComposition& hud_composition, ID3D11ShaderResourceView* owned_texture,
        bool transformed, bool specular);
    HRESULT apply_state(const FixedState& state, bool textured, const widescreen::HudComposition& hud = {}, const camp::Clip& clip = {});
    void restore_camp_clip() noexcept;
    struct CampClipScope {
        Renderer& renderer;
        ~CampClipScope() { renderer.restore_camp_clip(); }
    };
    void end_profile_pass();
    void transition_profile_pass(const char* name);
    // GPU sub-scope names (profiling only). Terrain parts: live, live after a
    // cached draw, cached, liquid ground, liquid water, foam, stock water.
    enum class TerrainProfilePart : unsigned { live, live_routed, cached, ground, water, foam, stock_water, live_flat };
    static const char* terrain_profile_scope(bool neighbour, bool tessellated, TerrainProfilePart part) noexcept;
    RendererResourceSnapshot resource_snapshot() const;
    // Someone reads the per-pass status text this frame: the overlay, a capture
    // warm-up, attribution, the hitch recorder, a harness route or critter labels.
    bool status_text_wanted() const noexcept;
    // Peak-sized CPU-mapped upload buffers and their CPU staging, dropped
    // when a new world begins so they regrow to that world's demand.
    void release_world_upload_peaks() noexcept;
    // Drops free instance records and reusable prepared slots from the end
    // of their tables and returns the storage once a world is gone.
    void trim_native_tables() noexcept;
    // World change (a new world epoch: map load or main menu), at the first
    // engine frame of the new world with no draw pending: every world-scoped
    // GPU resource and CPU store goes, then the device and heap are trimmed.
    // The new world rebuilds on demand at its own size.
    void release_world_state() noexcept;
    std::uint64_t world_state_epoch_ = 0;
    std::uint64_t scene_world_epoch_ = 0;
    UINT scene_frame_latency_ = 0;          // game thread: latency applied while in scene mode (0: none)
    UINT scene_frame_latency_restore_ = 0;  // the device's latency before scene mode
    void apply_scene_frame_latency() noexcept;
    // Scene mode: surface id -> the published texture the game thread still
    // holds (scene_publish_surface_texture; erased when it moves off it).
    mutable std::mutex scene_surface_holds_lock_;
    std::unordered_map<std::uint64_t,ID3D11Texture2D*> scene_surface_holds_; // game thread, scene mode: world_state_epoch_ as of the last inline frame begin
    HRESULT begin_engine_frame_at(std::uint64_t world_epoch);
    // Settings-window video memory by feature; summed at most every 15
    // presents while the window is open (cached in video_memory_).
    VideoMemoryBreakdown video_memory_breakdown() const;
    mutable VideoMemoryBreakdown video_memory_{};
    mutable std::uint64_t video_memory_frame_ = UINT64_MAX;
    std::uint64_t critter_video_bytes() const noexcept;  // renderer_critters.cpp
    // WDDM local video memory budget (renderer_vram_budget.cpp). Polled after
    // Present; enforced on adapters with a non-local segment (discrete) or
    // when EID3D11_VRAM_BUDGET_MB overrides the budget.
    struct VramBudget {
        bool known = false, enforced = false, uma = false, overridden = false;
        std::uint64_t budget = 0, usage = 0, override_bytes = 0;
        unsigned level = 0;  // VramBudgetSnapshot::level
        std::array<std::uint64_t, 4> usage_at_step{}, step_savings{};
        std::uint64_t next_poll_frame = 0, settle_until_frame = 0, evicted_bytes = 0;
        std::uint64_t rebuild_frame = UINT64_MAX, rebuild_bytes = 0;
        unsigned restore_polls = 0;
        bool cannot_fit_reported = false;
    } vram_budget_;
    void update_vram_budget() noexcept;
    std::uint64_t evict_idle_textures_for_budget(std::uint64_t bytes) noexcept;
    VramBudgetSnapshot vram_budget_snapshot() const noexcept;
    // Top mips a reducible world texture leaves out at the current level.
    UINT vram_texture_skip(UINT width, UINT height, UINT levels) const noexcept {
        if (!vram_budget_.level) return 0;
        UINT skip = vram_budget_.level >= 3 ? 2u : 1u;
        while (skip && ((std::max)(width, height) >> skip) < 64u) --skip;  // small textures stay whole
        return levels > skip ? skip : (levels ? levels - 1 : 0);
    }
    bool vram_take_rebuild(std::uint64_t bytes) noexcept;
    UINT vram_shadow_shift() const noexcept { return vram_budget_.level >= 2 ? 1u : 0u; }
    CutoutShadowScopeSnapshot cutout_shadow_scope_snapshot() const noexcept;

    HWND hwnd_ = nullptr;
    // World render size: the output scaled by the player render scale. Every
    // world pass, its depth and its screen-space targets use it.
    UINT width_ = 1;
    UINT height_ = 1;
    // Output (swap chain, interface) size; game pixels map 1:1 onto it.
    UINT output_width_ = 1;
    UINT output_height_ = 1;
    int render_scale_percent_ = 100;
    // Full size of the active colour target: world before the scene/UI
    // boundary, output after it.
    UINT canvas_width() const noexcept { return widescreen_interface_owned() ? UINT(widescreen_layout().width) : output_width_; }
    UINT canvas_height() const noexcept { return widescreen_interface_owned() ? UINT(widescreen_layout().height) : output_height_; }
    UINT target_width() const noexcept { return scene_composited_ || hit_test_active_ ? output_width_ : width_; }
    UINT target_height() const noexcept { return scene_composited_ || hit_test_active_ ? output_height_ : height_; }
    // The full viewport of the active colour target: always whole pixels.
    // The temporal upscalers' sub-pixel jitter is in the projection instead
    // (jitter_clip): a fractional viewport origin let the rasterizer's
    // integer viewport scissor drop the last row and column on negative
    // jitter phases (the flickering bottom edge with DLSS and FSR, depth view
    // of ab497e20).
    D3D11_VIEWPORT target_viewport() const noexcept {
        return D3D11_VIEWPORT{0, 0, float(target_width()), float(target_height()), 0, 1};
    }
    // A camera world-to-clip (row-major, clip = M * v) with this frame's
    // upscaler jitter (x pixels right, y pixels down) while the world pass
    // renders into the upscaler's input: not after the composite, not
    // without a temporal upscaler. Applied once, where each world pass
    // uploads its camera clip; light-space (shadow) clips never take it.
    bool upscaler_jitter_ndc(float& dx, float& dy) const noexcept {
        if (scene_composited_ || hit_test_active_ || !temporal_upscaler() || (dlss_jitter_[0] == 0.f && dlss_jitter_[1] == 0.f)) return false;
        dx = 2.f * dlss_jitter_[0] / float(target_width()); dy = -2.f * dlss_jitter_[1] / float(target_height());
        return true;
    }
    void jitter_clip(float* clip) const noexcept {
        float dx = 0, dy = 0;
        if (!upscaler_jitter_ndc(dx, dy)) return;
        for (int column = 0; column < 4; ++column) {
            clip[column] += dx * clip[12 + column];
            clip[4 + column] += dy * clip[12 + column];
        }
    }
    std::array<float, 16> jittered_clip(std::array<float, 16> clip) const noexcept { jitter_clip(clip.data()); return clip; }
    void apply_render_scale();
    // DLSS (upscaler/upscaler_quality, dlss_frame.h): direct native backend
    // in embedded, remote host in retail. Mode of the world targets, -1 off.
    int dlss_mode_ = -1;
    // AMD FSR 2 (the same options, fsr2_upscaler.h): the FSR mode the world
    // targets were built for (0 Quality .. 4 Native AA), -1 off. FSR and DLSS
    // share the render size, jitter, motion and reset path below.
    int fsr_mode_ = -1;
    Fsr2Upscaler fsr_;
    UpscaleSharpen upscale_sharpen_;  // upscale_sharpness after DLSS (FSR sharpens inside fsr_)
    std::chrono::steady_clock::time_point fsr_last_frame_{};  // FSR frame time (its delta time constant)
    bool temporal_upscaler() const noexcept { return dlss_mode_ >= 0 || fsr_mode_ >= 0; }
    float dlss_jitter_[2]{};
    std::uint32_t dlss_jitter_index_ = 0;
    bool dlss_reset_ = true;
    bool dlss_previous_valid_ = false;
    std::uint64_t dlss_last_evaluate_ms_ = 0;  // GetTickCount64 of the last evaluation
    std::array<float,16> dlss_previous_clip_{};
    // Object motion: last frame's world vertices (copied at the start of each
    // world frame, same slots), the motion stages and their constants.
    ComPtr<ID3D11Buffer> dlss_previous_world_, dlss_motion_constants_;
    ComPtr<ID3D11ShaderResourceView> dlss_previous_world_srv_;
    ComPtr<ID3D11VertexShader> native_motion_vs_;
    ComPtr<ID3D11PixelShader> native_motion_ps_;
    ComPtr<ID3D11DepthStencilState> dlss_motion_depth_;
    std::uint64_t dlss_motion_constants_frame_ = UINT64_MAX;
    void begin_dlss_object_motion() noexcept;
    bool draw_dlss_object_motion(UINT args_offset);
    bool dlss_motion_constants_current();  // b11 NativeMotionConstants for this frame (draw_dlss_object_motion, fur)
    int configured_dlss_mode() noexcept;
    int configured_fsr_mode() noexcept;
    void set_world_size() noexcept;
    void advance_dlss_jitter() noexcept;
    // One upscaler frame: camera motion and the reset decision (shared), then
    // DLSS (native embedded backend / retail host) or FSR 2 into the output-size image, then
    // that image to the output. False: the upscaler failed this frame.
    struct UpscalerFrame { bool camera = false, reset = true; float clip_to_world[16]{}; std::array<float,16> world_to_clip{}; };
    UpscalerFrame begin_upscaler_frame();
    void finish_upscaler_frame(const UpscalerFrame& frame);
    bool render_dlss();
    bool render_fsr();
    ComPtr<IDXGISwapChain> swap_;
    UINT swap_flags_=0;bool tearing_supported_=false;
    FrameLimiter frame_limiter_;  // player frame_limit option, paces Present
    std::uint32_t shader_groups_prepared_ = 0;  // prepare_shaders groups already created
    std::uint32_t shader_table_reported_ = UINT32_MAX;  // reachable groups last reported (shader_table event)
    std::uint32_t shader_on_demand_reported_ = 0;  // shader_cache on-demand compiles already logged
    // The device's prepared shader objects (shader_cache.h create_shader),
    // attached at device creation and detached before the device goes.
    ComPtr<ShaderObjects> shader_objects_;
    std::uint32_t shader_unprepared_reported_ = 0;  // shader_objects_ misses already logged
    // D3D11_FEATURE_THREADING DriverConcurrentCreates (report_adapter). False:
    // shader preparation runs on one worker, like after a parallel crash.
    bool driver_concurrent_creates_ = false;
    std::atomic<bool> shader_marker_open_{false};  // a preparation's breadcrumb awaits a completed present
    HRESULT create_swap_chain();
    ComPtr<ID3D11Device> device_;
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
    DlssBackend dlss_backend_; // destroyed before device_, also explicitly shut down by release()
#endif
    ComPtr<ID3D11DeviceContext> context_;
    // Per-draw constants (constant_ring.h, issue #162): appended with
    // NO_OVERWRITE and bound by range through context1_ (the immediate context as an
    // ID3D11DeviceContext1). Written only by the renderer's owner thread. Without
    // D3D11.1 constant buffer offsets and NO_OVERWRITE on dynamic constant
    // buffers, each block keeps its own buffer with one DISCARD per write.
    ComPtr<ID3D11DeviceContext1> context1_;
    owned::ConstantRing constant_ring_;
    bool constant_ring_supported_=false;
    // The last b0 draw constants (Constants), so a pass that rebinds b0
    // without writing (bind_draw_constants) still sees them after a restart.
    std::array<std::uint8_t,owned::ConstantRing::kAlign*2> draw_constants_copy_{};
    UINT draw_constants_bytes_=0;
    owned::ConstantRing::Range draw_constants_range_{};
    std::uint32_t draw_constants_generation_=0;
    enum ConstantStage : UINT { constant_vs=1u, constant_ps=2u };
    void refresh_constant_ring_context() noexcept;
    void restart_upload_rings() noexcept;
    HRESULT push_constants(UINT stages,UINT slot,const void* data,UINT bytes,ID3D11Buffer* own_buffer) noexcept;
    HRESULT upload_draw_constants(const void* data,UINT bytes) noexcept;
    void bind_draw_constants() noexcept;
    // False when a ring restart, wrap or regrowth since the last
    // upload_draw_constants left the bound b0 range stale (callers that skip
    // an unchanged upload must write again).
    bool draw_constants_live() const noexcept{
     return !(constant_ring_supported_&&context1_)||draw_constants_generation_==constant_ring_.generation();
    }
    // Scene thread (render_thread option). Switched only on the game thread
    // at a frame boundary, with the scene thread drained.
    std::unique_ptr<SceneThread> scene_thread_;
    std::atomic<bool> scene_mode_{false};
    // ---- Core mode state (x86 game process; renderer_game.cpp) ----------------
    bool core_mode_ = false;
    std::uint64_t core_present_serial_ = 0;      // presents sent (the host publishes presents_done)
    std::int64_t core_frame_start_qpc_ = 0;      // when the last present wait returned (Present::frame_start_qpc)
    HWND core_foreground_logged_ = reinterpret_cast<HWND>(~std::uintptr_t(0));  // the foreground window last logged
    std::uint64_t core_config_revision_ = ~0ull; // config revision last sent
    std::vector<std::uint8_t> core_envelope_;    // frame envelope last sent (unchanged ones are not resent)
    std::vector<std::pair<const char*, std::uint64_t>> core_refusals_, core_refusal_window_;
    std::uint64_t core_window_frames_ = 0;
    long core_harness_acks_read_ = 0;              // host harness acks retired
    long core_overlay_resets_seen_ = 0;           // Control::overlay_mirror_resets last applied
    long core_camp_answers_seen_[3] = {};          // Control::camp_answers last applied
    bool core_f11_down_ = false;                    // game process: F11 held at the last present
    std::vector<std::uint8_t> core_widescreen_sent_; // game process: WidescreenState last sent (encoded)
    std::vector<std::uint8_t> core_mounts_sent_;     // game process: ResourceMounts last sent (encoded)
    std::uint64_t core_combat_revision_sent_ = 0, core_combat_catalog_sent_ = 0, core_combat_font_sent_ = 0;
    void core_sync_combat() noexcept;                // game process: CombatState when the combat UI changed
    void core_send_mounts() noexcept;                // game process: ResourceMounts when they changed
    long core_config_saved_seen_ = 0;                // game process: host ini saves already reloaded
    std::uint64_t core_border_fog_seen_ = 0;          // game process: host border fog revision already published here
    long core_display_apply_seen_ = 0;               // game process: host display Apply already taken
    bool core_take_display_apply(DisplaySettings& settings) noexcept;
    void core_display_apply_result(const DisplaySettings& settings, HRESULT result, const char* message) noexcept;
    std::uint64_t core_va_epoch_ = 0;                // game process: world epoch of the last va_regions
    ULONGLONG core_va_due_ms_ = 0;                   // game process: when that epoch's va_regions is due (0: none)
    bool core_capture_request_ = false;             // host: an F11 capture the game process asked for
    // Glyph atlases: the (generation, revision) last sent per identity (game
    // process) and the latest atlas per identity (host).
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> core_glyphs_sent_;
    std::unordered_map<std::uint64_t, std::shared_ptr<const CharacterPaneGlyphAsset>> core_glyphs_;
    std::shared_ptr<const TerrainAtlasSnapshot> core_atlases_sent_; // terrain atlas snapshot last sent
    std::uint8_t core_game_status_sent_[4] = {0xff, 0, 0, 0};    // GameStatus last sent (sky, slots, sites; [3] unused)
    const char* core_weather_problem_sent_ = reinterpret_cast<const char*>(1);
    const char* core_camera_pan_unavailable_sent_ = reinterpret_cast<const char*>(1);
    std::string core_addon_notice_sent_; // compare text: the game's startup notice buffer is reused
    unsigned long core_harness_config_entry_ = 0;  // harness config step applied here
    // Game process: sends the surface's pending write to the host (SurfaceWrite)
    // and marks it uploaded; its CPU pixels stay (no GPU copy exists here).
    HRESULT core_surface_write(SurfaceImage& image);
    // Host, core mode: the game's surfaces by id. Each entry is the host's CPU
    // mirror and GPU home of the latest write it received.
    std::unordered_map<std::uint64_t, std::shared_ptr<SurfaceImage>> core_surfaces_;
    // Host: the handles of one command resolved through core_surfaces_ (a
    // handle whose generation the host does not hold resolves to nothing:
    // that quad or packet is omitted, logged once per surface).
    scene_ui::Residency core_residency(const std::vector<scene_ui::SurfaceHandle>& handles);
    // The bodies of queued UI payloads, shared by the standard-mode closures
    // and the core-mode host's run().
    void ui_run_blits(std::vector<scene_ui::Quad>& quads, const scene_ui::Residency& residency);
    void ui_run_present_image(scene_ui::PresentImage& command, const scene_ui::Residency& residency);
    void ui_run_draw_frame(scene_ui::DrawFrame& command, const scene_ui::Residency& residency);
    HRESULT core_ensure();
    HRESULT core_present();
    HRESULT core_resize(UINT width, UINT height);
    // Game process, core mode: the host's tight BGRA rows of the back buffer box
    // (back_buffer) or of a game surface's GPU home, chunked through the
    // readback area (synchronous: one round trip per chunk).
    HRESULT core_readback(bool back_buffer, std::uint64_t id, std::uint64_t generation, const RECT& box,
                          std::vector<std::uint8_t>& bgra);
    HRESULT core_capture(SurfaceImage& destination, const RECT& destination_rect, const RECT& native_source_rect);
    HRESULT core_readback_surface(SurfaceImage& image);
    // Host, core mode: the packed copy a chunked Readback serves.
    std::vector<std::uint8_t> core_readback_copy_;
    void core_send_config() noexcept;
    void core_send(std::uint16_t command, const std::vector<std::uint8_t>& bytes) noexcept;
    void report_core_refusal_window() noexcept;
    // The option list and the startup values of the loaded options (ensure,
    // core_ensure and ensure_host share them).
    void register_config_options();
    void apply_config_settings();
    std::vector<std::pair<const char*,std::uint64_t>> scene_syncs_; // game thread: reasons, first logged
    unsigned scene_inline_=0;        // game thread: depth of SceneInlineScope (calls run inline)
    std::vector<std::pair<const char*,std::uint64_t>> scene_sync_window_; // game thread: waits this window
    std::uint64_t scene_window_frames_=0; // game thread: presents queued in this window
    bool scene_ui_pending_=false;    // game thread: UI queued since the last UI flush
    bool scene_native_pending_=false;// game thread: native work queued since the last native flush
    std::uint64_t scene_checkpoint_serial_=0;                 // game thread
    std::unordered_map<std::uint64_t,NativeQueueCheckpoint> scene_checkpoints_; // scene thread
    bool clear_rects_placed_=false;  // scene thread: clear() rectangles are already camp-placed
    // Surface uploads the game thread makes in scene mode record into this
    // deferred context; the list runs on the scene thread ahead of the next
    // queued command, after any queued output blit from the same surfaces.
    ComPtr<ID3D11DeviceContext> game_upload_context_;
    bool game_upload_pending_=false;
    std::vector<std::uint64_t> game_upload_surfaces_;
    std::uint64_t game_upload_bytes_=0;
    HRESULT game_upload(SurfaceImage& image,SurfaceUploadResult* output);
    void flush_game_uploads() noexcept;
    // Scene thread: a snapshot whose world use asks for mips its game texture
    // lacks gets one mipped copy per (surface, generation, texture object).
    struct SceneMipTexture{std::uint64_t generation=0,texture_serial=0;ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;D3D12TextureRef d3d12;};
    std::unordered_map<std::uint64_t,SceneMipTexture> scene_mip_textures_;
    std::uint64_t scene_mode_frame_=0;   // presentation frame of the last activation
    void set_scene_mode(bool active) noexcept;
    void report_scene_sync_window() noexcept;
public:
    // Game thread, scene mode: the selection result (screen extents and
    // visibility) queue_native/queue_native_owned compute for a non-world
    // packet, from the packet alone: its position bounds plus morph range,
    // the HUD composition of its clip, and the state's viewport. S_OK
    // visible, S_FALSE outside, failure as the queue would fail.
    HRESULT native_packet_extents(const NativeGeometryView& view,const FixedState& st,RECT& extents) const noexcept;
    // A queued call's failure: one line per operation, at most a few each.
    static void report_scene_failure(const char* operation,HRESULT hr) noexcept;
private:
    void begin_scene_game();
    void begin_scene_renderer();
    HRESULT queue_present();
    HRESULT present_frame(const ScenePresentInputs* inputs);
    ComPtr<IDXGIAdapter3> adapter3_;
    // "adapter=<name> vendor= device= driver=<a.b.c.d>", recorded at device creation.
    std::string adapter_summary_;
    void report_adapter() noexcept;
    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_9_3;
    // Active colour target: scene until the first interface boundary, then backbuffer.
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11RenderTargetView> backbuffer_rtv_;
    SceneComposite scene_composite_;
    BloomPass bloom_; // world-only bloom added at the scene/UI boundary
    std::uint64_t present_failures_[3]{}; // failed present steps: native flush, composition, UI blits
    float bloom_fog_headroom_ = 0.f; // volumetric shaft headroom of this frame's world fog (bloom_ceiling)
    SunRaysPass sun_rays_; // screen-space sun rays at the same boundary (sun_shafts.h)
    DepthOfFieldPass depth_of_field_; // far-field blur for tilted cameras, before sun rays and bloom
    bool scene_composited_ = false;
    bool scene_updated_ = false;
    // depth_/depth_srv_ are the world depth. dsv_ is the active depth view:
    // world_dsv_ until the scene/UI boundary, then interface_dsv_ (the same
    // view at 100% render scale, else an output-size copy of the world depth).
    ComPtr<ID3D11Texture2D> depth_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11ShaderResourceView> depth_srv_;
    ComPtr<ID3D11DepthStencilView> world_dsv_;
    // Zone hit-test frame (#200, hit_test_frame): output-size B8G8R8A8 colour
    // and its own depth. While active the world draws flat default colours
    // (nativeCombinerTail) into it, is never composited, and the game's
    // captures read it (capture_source) instead of the processed back buffer.
    ComPtr<ID3D11Texture2D> hit_test_color_,hit_test_depth_;
    ComPtr<ID3D11RenderTargetView> hit_test_rtv_,hit_test_saved_rtv_;
    ComPtr<ID3D11DepthStencilView> hit_test_dsv_,hit_test_saved_dsv_;
    UINT hit_test_width_=0,hit_test_height_=0;
    // D3D12: the same target (B8G8R8A8 with its own D24S8 depth); between
    // begin and end it stands in for world_d3d12_, which waits in the saved slot.
    D3D12TextureRef hit_test12_,hit_test_saved_world12_;
    bool hit_test_active_=false;
    bool hit_test_failed_=false;
    void hit_test_frame_apply(bool begin) noexcept;
    // The capture source: the hit-test target during that frame, else the
    // composited back buffer (composite_world and the pending UI first).
    HRESULT capture_source(ComPtr<ID3D11Texture2D>& source);
    ComPtr<ID3D11Texture2D> interface_depth_;
    ComPtr<ID3D11DepthStencilView> interface_dsv_;
    // One colour snapshot at the first liquid batch, owned through target resize.
    struct WaterReflectionConstants {
        std::array<float,16> clip{},inverse{};
        std::array<float,4> target{};
        std::array<float,4> quality{}; // SSR march steps (water_reflection_quality), history mip, history valid, reserved
        std::array<float,16> history_clip{}; // world-to-clip of the previous frame's colour (slice 1)
        std::array<float,16> history_inverse{}; // its inverse, for the previous frame's particle depth
        std::array<float,4> march{}; // largest coarse SSR step in target pixels (water_reflection_step_pixels; 0: fixed), reserved
    };
    int water_reflection_quality_logged_ = -1; // last quality written to the event log
    // Mip levels of the reflection scene copy (1 when the format has no mip
    // generation); kWaterSceneMipLevels caps the chain.
    static constexpr UINT kWaterSceneMipLevels = 6;
    UINT water_scene_levels_ = 1;
    ComPtr<ID3D11ShaderResourceView> water_history_srv_; // slice 1 of water_scene_color_, for its mips
    std::array<float,16> water_history_clip_{},water_history_inverse_{};
    std::uint64_t water_history_frame_ = UINT64_MAX;
    // Particle depth for reflections (#140): nearest depth of each pixel's
    // significant world game particle, written by a second draw of every
    // world particle call after the game's water. Two slots: the frame's
    // writes go to particle_depth_current_; capture_water_history hands that
    // slot to the next frame's reflections (with water_history_clip_).
    ComPtr<ID3D11Texture2D> particle_depth_[2];
    ComPtr<ID3D11RenderTargetView> particle_depth_rtv_[2];
    ComPtr<ID3D11ShaderResourceView> particle_depth_srv_[2];
    UINT particle_depth_current_ = 0, particle_depth_width_ = 0, particle_depth_height_ = 0;
    std::uint64_t particle_depth_frame_ = UINT64_MAX;          // frame whose writes the current slot holds
    std::uint64_t particle_depth_history_frame_ = UINT64_MAX;  // frame whose writes the other slot holds
    ComPtr<ID3D11PixelShader> game_particle_depth_ps_;
    ComPtr<ID3D11BlendState> particle_depth_blend_;
    ComPtr<ID3D11DepthStencilState> particle_depth_test_;
    bool particle_depth_failed_ = false;
    bool particle_depth_wanted() const;
    bool ensure_particle_depth_pipeline();
    void draw_game_particle_depth(UINT vertex_count);
    // D3D12 backend: the same two R32F slots as targets, and the second draw
    // inside draw_game_effect_d3d12 (its vertex stage and inputs still bound).
    std::array<D3D12TextureRef,2> particle_depth12_;
    HRESULT draw_game_particle_depth_d3d12(const ShaderProgram& vertex,UINT vertex_count,const D3D12NativeState& particle);
    ComPtr<ID3D11Texture2D> water_scene_color_;
    ComPtr<ID3D11ShaderResourceView> water_scene_srv_;
    ComPtr<ID3D11Texture2D> water_scene_depth_;ComPtr<ID3D11ShaderResourceView> water_scene_depth_srv_;
    ComPtr<ID3D11DepthStencilView> water_scene_dsv_; // the pre-water depth copy, for ground marks drawn after the water
    ComPtr<ID3D11Buffer> water_reflection_constants_;
    ComPtr<ID3D11Buffer> liquid_ripple_constants_;
    std::uint64_t liquid_ripple_frame_ = UINT64_MAX;
    bool liquid_ripple_ready_ = false, liquid_ripple_failure_reported_ = false;
    void prepare_liquid_ripples();
    // Per liquid batch, the frame's wading units that can reach its water
    // (bind_liquid_ripple_units): the frame's values and each unit's reach,
    // and the last culled list written to the batch buffer.
    NativeLiquidRipples liquid_ripple_values_{};
    std::array<float,16> liquid_unit_reach_{};
    std::uint64_t liquid_ripple_values_serial_=0,liquid_ripple_batch_serial_=0;
    std::array<std::uint8_t,17> liquid_ripple_batch_list_{};
    ComPtr<ID3D11Buffer> liquid_ripple_batch_constants_;
    float native_terrain_wind_force_=0; // the terrain frame's live wind force (nativeTerrainWind.z)
    void bind_liquid_ripple_units(const float lo[2],const float hi[2]);
    // Interactive ripple simulation: a wave-equation height field in a window
    // that follows units in water; they displace it and trails propagate.
    ComPtr<ID3D11Texture2D> liquid_wave_texture_[2];
    ComPtr<ID3D11ShaderResourceView> liquid_wave_srv_[2];
    ComPtr<ID3D11UnorderedAccessView> liquid_wave_uav_[2];
    ComPtr<ID3D11ComputeShader> liquid_wave_cs_;ComPtr<ID3D11Buffer> liquid_wave_constants_;
    unsigned liquid_wave_current_=0;bool liquid_wave_failed_=false,liquid_wave_origin_valid_=false;
    std::array<int,2> liquid_wave_origin_{};double liquid_wave_clock_=-1,liquid_wave_accumulator_=0;
    std::uint64_t liquid_wave_map_generation_=0; // ambient_map_generation_ the field belongs to (a new map starts calm)
    void step_liquid_waves(NativeLiquidRipples& values,double seconds);
    // Per unit wake easing (the one per-unit water state): the running wake
    // and the parting fade in and out on game time instead of popping with the
    // unit's speed or its entry into and exit from the water. Units that left
    // keep their last values while they fade (renderer_liquid.cpp).
    struct LiquidUnitEase {
        std::uint32_t id=0;float motion=0,presence=0;bool seen=false;
        float heading=0;bool heading_valid=false; // eased wedge heading (radians)
        std::array<float,4> position_speed{},phase{},flow{};
    };
    std::vector<LiquidUnitEase> liquid_unit_ease_;double liquid_ease_clock_=-1;
    void ease_liquid_units(NativeLiquidRipples& values,double seconds);
    // Sea surf field (liquid_surf.h): the active map's on a worker started at
    // its first liquid frame, each neighbour's from its stream worker, all
    // composited into one world-space texture (t62) whose window rides the
    // ripple constants (NativeLiquidRipples::surf_window). Released with the
    // map and the neighbours; maps without surf leave the texture.
    struct LiquidSurfMap { std::array<float,2> origin{}; std::string name; std::shared_ptr<const LiquidSurfField> field; };
    std::future<std::shared_ptr<const LiquidSurfField>> liquid_surf_job_;
    std::uint64_t liquid_surf_generation_=UINT64_MAX; // map generation of the active field (or its job)
    std::shared_ptr<const LiquidSurfField> liquid_surf_active_;
    std::vector<LiquidSurfMap> liquid_surf_neighbours_;
    std::uint64_t liquid_surf_serial_=1,liquid_surf_built_serial_=0; // composite inputs changed / composited
    ComPtr<ID3D11ShaderResourceView> liquid_surf_srv_,liquid_surf_level_srv_; // t62 field, t63 sea level
    std::array<float,4> liquid_surf_window_{}; // origin xy (world m), 1/extent xy
    float liquid_surf_fade_=-1; // water_surf_edge_fade the composite was built with (m)
    // The composite's one regional swell direction (unit xy, from open sea
    // toward the shores; z its agreement 0..1): the swell runs as long-crested
    // plane waves along it instead of rings around the coastline.
    std::array<float,4> liquid_surf_swell_{};
    void update_liquid_surf() noexcept;
    void add_liquid_surf_neighbour(const NeighbourPiece& upload,const std::array<float,16>& active_to_world) noexcept;
    // Ambient particles (renderer_ambient_particles.cpp): camera-local,
    // area-driven motes/leaves/embers drawn into the world colour after the
    // world pass and before SMAA and the interface. Off creates nothing.
    void render_ambient_particles();
    bool rebuild_ambient_particle_map(const AmbientWorldSource& inputs,const WorldEnvironment& environment) noexcept;
    ComPtr<ID3D11VertexShader> ambient_vs_;ComPtr<ID3D11PixelShader> ambient_ps_;
    ComPtr<ID3D11Buffer> ambient_constants_;ComPtr<ID3D11BlendState> ambient_blend_;
    ComPtr<ID3D11RasterizerState> ambient_raster_;
    // Shared with the ground cover: area classes, ground/liquid-surface height
    // and base ground height, rebuilt per terrain source (generation counter).
    ComPtr<ID3D11ShaderResourceView> ambient_area_srv_,ambient_height_srv_,ambient_ground_srv_,ambient_type_srv_;
    // Per tile spray, cave drip and mushroom sources, and their CPU copy.
    ComPtr<ID3D11ShaderResourceView> ambient_sources_srv_;std::vector<std::array<std::uint8_t,4>> ambient_sources_;
    // Waterfall tiles the waterfall emitters fill (ambient_fall_tiles, t4).
    ComPtr<ID3D11ShaderResourceView> ambient_fall_tiles_srv_;std::uint32_t ambient_fall_tile_count_=0,ambient_fall_impact_count_=0;
    // Procedural falls of the map (procedural_falls.h) for fall foam, lip spray and mist (t5).
    ComPtr<ID3D11ShaderResourceView> ambient_falls_srv_;std::uint32_t ambient_fall_count_=0;
    // Waterfall shells of the map's falls (procedural_falls.h build_fall_shell, t6),
    // drawn before the particles with their own shaders and a read-only depth test.
    ComPtr<ID3D11ShaderResourceView> ambient_fall_shell_srv_;std::uint32_t ambient_fall_shell_vertices_=0;
    ComPtr<ID3D11VertexShader> fall_shell_vs_;ComPtr<ID3D11PixelShader> fall_shell_ps_;
    ComPtr<ID3D11DepthStencilState> fall_shell_depth_;
    // D3D12 backend: the same grids and waterfall lists (rebuilt with the
    // map, area/height rewritten on a layer shift) and the composite_world
    // effects' programs (append_*_shader_programs order).
    struct AmbientD3D12 {
        D3D12TextureRef area,height,ground,sources;
        D3D12BufferRef fall_tiles,falls,shells;
        std::vector<ShaderProgram> ambient,wind,precipitation;
        D3D12TextureRef flakes;  // the game's weather atlas (snow crystals)
    } ambient12_;
    bool rebuild_ambient_grids_d3d12(const TerrainCpuAsset& terrain,const std::vector<float>& ground) noexcept;
    // Every composite_world effect on D3D12 draws into the world colour with
    // the world depth read at t2 (depth_test: tested read-only instead), the
    // fog frame (b10), sky inputs and s7 bound; premultiplied alpha, no cull.
    D3D12NativeState world_effect_state_d3d12(bool depth_test) const noexcept;
    void bind_world_effect_d3d12(bool depth_test);
    // The world colour and depth those effects need exist this frame (either backend).
    bool world_effect_targets() const noexcept { return d3d12_ ? world_d3d12_bound_ && world_d3d12_ : depth_srv_ && rtv_; }
    std::weak_ptr<const OwnedMapSnapshot> ambient_source_map_;
    std::uint64_t ambient_map_generation_=0;
    std::weak_ptr<const TerrainCpuAsset> ambient_terrain_;
    std::shared_ptr<const OwnedTerrainGeometryValues> ambient_geometry_;
    std::vector<std::uint8_t> ambient_area_;std::vector<float> ambient_height_;
    // Static inputs of the level-dependent grid values: a layer shift step
    // updates heights/wet classes/emissive tiles in place (bounded).
    owned::ambient::LiquidLevelInputs ambient_liquid_level_;
    std::uint64_t ambient_liquid_level_updates_=0;
    bool update_ambient_liquid_level(const std::vector<float>& shifts) noexcept;
    // Emissive surfaces as soft point lights (emissive_lights.h): the active
    // map's wet self-illuminated liquid tiles (from the shared grids) and the
    // lights clustered from them for the current F10 level, appended to the
    // game's point lights in set_native_point_lights.
    std::vector<emissive::Tile> emissive_tiles_;
    std::vector<emissive::Cluster> emissive_clusters_;
    std::uint64_t emissive_generation_=UINT64_MAX,emissive_update_frame_=UINT64_MAX;int emissive_level_=-1;
    // Per cluster: the mean rendered lava colour (emissive::rendered_color),
    // from the liquid detail map's heat samples once they exist.
    std::vector<std::array<float,3>> emissive_colors_;bool emissive_colors_sampled_=false;
    // {heat, brightness} samples of the lava the liquid shader draws, taken
    // from its generated detail map (renderer_liquid.cpp).
    std::vector<std::array<float,2>> liquid_lava_samples_;
    // Emissive lights in the latest published light list (F11 status/JSON).
    std::size_t emissive_published_=0;
    void refresh_emissive_lights() noexcept;
    std::uint32_t ambient_tiles_x_=0,ambient_tiles_y_=0,ambient_zone_=0;bool ambient_menu_=false;
    std::string ambient_map_name_; // map file basename, lower case (rebuild_ambient_particle_map)
    // water_surf_no_sea_maps: maps whose water never counts as sea (liquid_surf.h).
    std::string liquid_surf_no_sea_maps() const {
        const auto* option=debug_overlay_.config().find("water_surf_no_sea_maps");
        return option?option->text_value:std::string("zone7");
    }
    std::array<std::array<double,3>,15> ambient_displacement_{};
    double ambient_time_=-1;int ambient_level_=-1;bool ambient_failed_=false;
    float ambient_rain_=0.f,ambient_snow_=0.f;double ambient_weather_hours_=-1;
    double ambient_sand_front_=0;  // integrated sand gust front position, metres
    NativeTerrainState ambient_terrain_state_{};std::uint64_t ambient_terrain_frame_=UINT64_MAX;
    WeatherWindState weather_wind_{};std::uint64_t weather_wind_frame_=UINT64_MAX;
    std::uint32_t ambient_fog_color_=0;
    // Ground cover (renderer_ground_cover.cpp): GPU-placed grass and debris in
    // the opaque world pass after terrain. Level 0 creates and reads nothing.
public:
    static constexpr unsigned kGroundCoverMeshes = 34;
private:
    HRESULT render_ground_cover(const FixedState& frame,const std::array<float,16>& world_to_clip) noexcept;
    HRESULT render_ground_cover_d3d12(const FixedState& frame,const std::array<float,16>& world_to_clip) noexcept;
    // The sun shadow pass (flush_native_shadows, a close map's per-frame layer):
    // the previous frame's cover placement and critters as depth casters.
    void draw_ground_cover_shadows(UINT slice) noexcept;
    std::uint64_t gc_shadow_caster_mask(int level) const noexcept;
    void draw_critter_shadows(UINT slice) noexcept;
    // D3D12: the same casters for the D3D12 shadow flush (renderer_critters.cpp contract).
    void draw_ground_cover_shadows_d3d12(UINT slice,const RECT* scissor=nullptr) noexcept;
    void draw_critter_shadows_d3d12(UINT slice,const RECT* scissor=nullptr) noexcept;
    // Casters that move every frame (critters drawn last frame; swaying cover at
    // ground_cover_shadows 2): the close maps' per-frame layer is redrawn
    // each frame while they exist, so their shadows follow them.
    bool owned_shadow_casters_animate() const noexcept;
    // What the close maps' renderer-owned casters were last drawn with (cover
    // level and placement ready, critters on and drawn): a change redraws
    // them once, so a setting change shows in a still or paused scene.
    std::uint32_t owned_shadow_casters_key() const noexcept;
    std::uint32_t native_shadow_extras_key_ = 0;
    HRESULT ensure_ground_cover_pipeline() noexcept;
    HRESULT update_ground_cover_maps(const AmbientWorldSource& inputs) noexcept;
    void release_ground_cover() noexcept;
    // World-change parts of release_world_state (shaders and states stay).
    void release_ground_cover_world() noexcept;
    void release_critters_world() noexcept;
    void release_ambient_world() noexcept;
    HRESULT update_contact_blend_maps() noexcept;
    void release_contact_blend_maps() noexcept;
    // Terrain cliff tessellation stages (terrain_cliff.h), compiled on first use.
    HRESULT ensure_terrain_tessellation() noexcept;
    ComPtr<ID3D11VertexShader> terrain_tess_vs_;ComPtr<ID3D11HullShader> terrain_hs_;ComPtr<ID3D11DomainShader> terrain_ds_;
    // The same stages without the snow trail code (nativeTerrainHSPlain,
    // nativeTerrainDSPlain), bound while no trail field exists.
    ComPtr<ID3D11HullShader> terrain_hs_plain_;ComPtr<ID3D11DomainShader> terrain_ds_plain_;
    bool terrain_tessellation_failed_=false;
    std::uint64_t terrain_tessellation_frame_=UINT64_MAX; // terrain frame whose constants enable it
    // Ground cover placement, every frame: a cell pass (each window cell once)
    // feeds per-class jobs in rounds through three stages per class (cells,
    // habitat, build; renderer_ground_cover.cpp kPlaceClasses), each class
    // compiled with only its rules' features; neighbours run all rules in one
    // set of three stages.
    static constexpr unsigned kGcPlaceClasses = 6, kGcPixelKinds = 5;
    // [stage][class]: cells, habitat, build (with trails), build without trails;
    // gc_place_cs_ (the plain cell stage) is set last and marks the pipeline complete.
    std::array<std::array<ComPtr<ID3D11ComputeShader>,kGcPlaceClasses>,4> gc_stage_cs_;ComPtr<ID3D11ComputeShader> gc_place_cs_;
    ComPtr<ID3D11ComputeShader> gc_cells_cs_,gc_scan_cs_,gc_neighbour_cs_,gc_habitat_all_cs_,gc_build_all_cs_;
    std::array<ComPtr<ID3D11Buffer>,kGcPlaceClasses> gc_class_constants_;std::array<UINT,kGcPlaceClasses> gc_class_rule_count_{};
    std::array<std::vector<std::uint32_t>,kGcPlaceClasses> gc_class_rules_; // rule indices per class, table order
    // Per-frame cell jobs per class, their counts, and per class and round the
    // dispatch arguments and first job (gcCellsCS, gcScanCS); each rule's class.
    ComPtr<ID3D11Buffer> gc_cell_jobs_,gc_cell_counts_,gc_rounds_,gc_rule_class_;
    ComPtr<ID3D11UnorderedAccessView> gc_cell_job_uav_,gc_cell_count_uav_,gc_round_uav_;
    ComPtr<ID3D11ShaderResourceView> gc_cell_job_srv_,gc_cell_count_srv_,gc_round_srv_,gc_rule_class_srv_;
    UINT gc_cell_job_capacity_=0;
    std::array<ComPtr<ID3D11Buffer>,64> gc_round_constants_; // GcRun per round (the round index)
    // Per-cell rule masks of the active map (every rule that can place in a
    // cell: a superset of the shader's acceptance) bound the stage buffers exactly.
    std::vector<std::uint8_t> gc_zone_uploaded_;   // the zone pieces last written to gc_zone_pieces_
    std::weak_ptr<const TerrainCpuAsset> gc_height_terrain_;float gc_height_range_[2]{}; // its sectors' lowest and highest ground
    std::vector<std::uint64_t> gc_cell_masks_;std::uint64_t gc_cell_masks_key_=UINT64_MAX;ComPtr<ID3D11ShaderResourceView> gc_cell_mask_srv_;
    HRESULT gc_ensure_stage_buffers(UINT capacity);
    HRESULT gc_ensure_cell_jobs(UINT cells);
    // Active map per tile: area class byte | base type << 8 | tree channels (r g b a nonzero) << 16 | Terrain 2.0 classes reachable (gc_t2_tile_classes_) << 24.
    std::vector<std::uint32_t> gc_tile_classes_;std::uint64_t gc_tile_classes_key_=UINT64_MAX;
    std::vector<std::uint8_t> gc_tree_mask_;              // active map per tile: tree channels nonzero
    std::uint64_t gc_tree_epoch_=0;                      // counts rebuilds of gc_tree_mask_
    // Ground cover on Terrain 2.0 (terrain2_cover.h), active map, terrain2 on:
    // each drawn material cell's cover class (from its rows build), per tile
    // the classes whose cells the 2.0 field can reach there (gc_cell_rule_masks),
    // and the pass's class shares and colour map, rebuilt when a key input changes.
    ComPtr<ID3D11ShaderResourceView> gc_t2_cell_class_srv_;std::vector<std::uint8_t> gc_t2_tile_classes_;std::uint64_t gc_t2_rows_generation_=0;
    ComPtr<ID3D11ShaderResourceView> gc_t2_classes_srv_,gc_t2_color_srv_;ComPtr<ID3D11UnorderedAccessView> gc_t2_classes_uav_,gc_t2_color_uav_;
    ComPtr<ID3D11ComputeShader> gc_t2_cs_;ComPtr<ID3D11Buffer> gc_t2_constants_;
    std::uint64_t gc_t2_key_=0;bool gc_t2_valid_=false,gc_t2_failed_=false;
    void update_terrain2_cover_classes(const TerrainTileFamilies& families,const std::map<std::uint32_t,std::uint32_t>& cells,
                                       const std::vector<std::array<std::uint32_t,4>>& rows,UINT w,UINT h) noexcept;
    void update_ground_cover_terrain2() noexcept;
    void release_ground_cover_terrain2() noexcept;
    // D3D12: the same cell classes (a typed R32_UINT buffer), class shares and
    // colour map (storage textures), and the pass's program.
    D3D12BufferRef gc_t2_cell_class12_;D3D12TextureRef gc_t2_classes12_,gc_t2_color12_;std::vector<ShaderProgram> gc_t2_programs12_;
    void update_ground_cover_terrain2_d3d12() noexcept;
    // The cover's terrain colour: terrain 2.0's (null until its pass ran).
    ID3D11ShaderResourceView* ground_cover_color_srv() const noexcept{return gc_t2_color_srv_.Get();}
    bool fog_cloud_visible_=false;                       // this frame's fog constants carry a cloud shadow map (bind_world_fog)
    ComPtr<ID3D11Buffer> gc_work_;ComPtr<ID3D11UnorderedAccessView> gc_work_uav_;ComPtr<ID3D11ShaderResourceView> gc_work_srv_; // placed-instance work per pass
    ComPtr<ID3D11ComputeShader> gc_finalize_cs_,gc_paint_cs_,gc_fields_cs_;
    // Settings permutations: shade [far maps].
    std::array<ComPtr<ID3D11ComputeShader>,2> gc_shade_cs_;
    ComPtr<ID3D11ComputeShader> gc_caster_list_cs_; // gcCasterListCS: the sun casters of a pass (per-instance cull)
    // Stage buffers (demand-sized from the count readback): candidates of the
    // cell stage, survivors of the habitat stage, their counts and dispatch
    // arguments (one buffer per producing stage), and the sun shadow inputs
    // per pool slot (gcShadeCS).
    ComPtr<ID3D11Buffer> gc_candidates_,gc_survivors_,gc_shade_input_;std::array<ComPtr<ID3D11Buffer>,2> gc_stage_counts_;
    ComPtr<ID3D11UnorderedAccessView> gc_candidate_uav_,gc_survivor_uav_,gc_shade_input_uav_;
    std::array<ComPtr<ID3D11UnorderedAccessView>,2> gc_stage_count_uav_;std::array<ComPtr<ID3D11ShaderResourceView>,2> gc_stage_count_srv_;
    UINT gc_candidate_capacity_=0,gc_survivor_capacity_=0;
    // Draw permutations: vertex (cover, cover with point shadows, snow pile),
    // pixel (procedural, bark, stone, snow pile Low, snow pile); each mesh's
    // kind from the materials of its rules. gcPrepCS (active, neighbour)
    // writes the per-frame draw inputs per pool slot (fog, cloud, wind).
    std::array<ComPtr<ID3D11VertexShader>,4> gc_vs_;std::array<ComPtr<ID3D11PixelShader>,60> gc_ps_;
    std::array<ComPtr<ID3D11ComputeShader>,8> gc_prep_cs_;
    // Depth prepass permutations (cover, snow pile): the material programs' placement, alpha test only.
    std::array<ComPtr<ID3D11VertexShader>,4> gc_depth_vs_;std::array<ComPtr<ID3D11PixelShader>,3> gc_depth_ps_;
    // Sun shadow casters (ground_cover_shadows): the placement vertex program
    // into the sun map (cover; piles never cast), the active map's per-mesh draw
    // counts and frame constants kept after its draws (the neighbour passes
    // reset the shared counts), whether that placement is complete, and the
    // large meshes (level 1: the rule table's widest LOD class).
    ComPtr<ID3D11VertexShader> gc_shadow_vs_;
    // One placement pass's casters (the active map, then each neighbour map):
    // its per-mesh counts (args; near_args: the far tuft meshes with their near
    // mesh's indices), its frame constants and ground, and copies of the pools
    // the later passes overwrite (instances and draw inputs at base, per mesh
    // in copy_mask). The next frame's shadow pass draws them (gc_record_casters).
    struct GcCasterPass {
        ComPtr<ID3D11Buffer> frame;
        ComPtr<ID3D11ShaderResourceView> ground;
        bool ready=false;
    };
    GcCasterPass gc_main_casters_;
    std::vector<GcCasterPass> gc_neighbour_casters_;
    bool gc_record_casters(GcCasterPass& pass,const void* constants,UINT constant_bytes,
                           ID3D11ShaderResourceView* ground) noexcept;
    // Sun caster lists (gcCasterListCS): per pool slot the slot of a casting
    // instance, in the same per-pass regions as the pool, and per pass and mesh
    // indirect draw arguments (the far tuft meshes with their near mesh's
    // indices) whose instance count is the list length.
    ComPtr<ID3D11Buffer> gc_caster_slots_,gc_caster_args_;
    std::array<ComPtr<ID3D11ShaderResourceView>,3> gc_caster_slot_srv_; // per close map: its third of the slot lists
    ComPtr<ID3D11UnorderedAccessView> gc_caster_slot_uav_,gc_caster_args_uav_;
    std::vector<std::array<UINT,5>> gc_caster_args_template_;  // per pass, close map and mesh, the list counts zero
    UINT gc_caster_args_passes_=0;
    bool gc_shadow_ready_=false;
    std::uint64_t gc_large_mesh_mask_=0;
    ComPtr<ID3D11Buffer> gc_draw_inputs_;ComPtr<ID3D11ShaderResourceView> gc_draw_input_srv_;ComPtr<ID3D11UnorderedAccessView> gc_draw_input_uav_;
    std::array<std::uint8_t,kGroundCoverMeshes> gc_mesh_kind_{};
    ComPtr<ID3D11VertexShader> gc_trail_vs_,gc_trail_full_vs_,gc_object_vs_;ComPtr<ID3D11PixelShader> gc_trail_ps_,gc_trail_decay_ps_,gc_object_ps_;
    ComPtr<ID3D11Buffer> gc_frame_constants_,gc_paint_constants_;
    std::vector<std::array<ComPtr<ID3D11Buffer>,kGroundCoverMeshes>> gc_draw_constants_; // per mesh pool base; rebuilt with the pool
    ComPtr<ID3D11Buffer> gc_vertices_,gc_indices_,gc_rules_,gc_args_,gc_counts_,gc_instances_;
    ComPtr<ID3D11ShaderResourceView> gc_vertex_srv_,gc_rule_srv_,gc_instance_srv_,gc_fields_srv_,gc_color_srv_,gc_tiles_srv_;
    ComPtr<ID3D11ShaderResourceView> gc_bark_srv_,gc_stone_srv_,gc_trees_srv_;
    ComPtr<ID3D11Buffer> gc_zone_pieces_;
    ComPtr<ID3D11ShaderResourceView> gc_zone_srv_;
    ComPtr<ID3D11UnorderedAccessView> gc_args_uav_,gc_counts_uav_,gc_instance_uav_,gc_color_uav_;
    // Per placement pass (active map, then each neighbour) and mesh: demand
    // this frame and the overflow keep fraction (gcOverflow), GPU only.
    ComPtr<ID3D11Buffer> gc_overflow_;ComPtr<ID3D11UnorderedAccessView> gc_overflow_uav_;UINT gc_overflow_passes_=0;
    ComPtr<ID3D11Texture2D> gc_color_;
    ComPtr<ID3D11Texture2D> gc_object_mask_;
    ComPtr<ID3D11RenderTargetView> gc_object_rtv_;
    ComPtr<ID3D11ShaderResourceView> gc_object_srv_,gc_object_input_srv_;
    ComPtr<ID3D11Buffer> gc_objects_;
    std::vector<ground_cover::FootprintSplat> gc_object_input_;
    UINT gc_object_capacity_=0,gc_object_width_=0,gc_object_height_=0;
    // The mask holds gc_object_drawn_ at this window origin; redrawn only when
    // the footprints, origin or texture change.
    std::vector<ground_cover::FootprintSplat> gc_object_drawn_;
    int gc_object_origin_x_=0,gc_object_origin_y_=0;bool gc_object_mask_current_=false;
    // Ping-pong world-anchored R16 trail state (ground_cover::trail_step).
    std::array<ComPtr<ID3D11Texture2D>,2> gc_trail_;
    std::array<ComPtr<ID3D11RenderTargetView>,2> gc_trail_rtv_;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> gc_trail_srv_;
    ComPtr<ID3D11Texture2D> gc_trail_sources_;
    ComPtr<ID3D11RenderTargetView> gc_trail_sources_rtv_;
    ComPtr<ID3D11ShaderResourceView> gc_trail_sources_srv_;
    ComPtr<ID3D11ShaderResourceView> gc_footprint_srv_;
    ComPtr<ID3D11Buffer> gc_footprints_;
    ComPtr<ID3D11BlendState> gc_trail_blend_;
    ComPtr<ID3D11DepthStencilState> gc_trail_depth_;
    // Count readbacks in flight, oldest first (head); a slot is copied into
    // again only after it was read. Deeper than the frame latency.
    std::array<ComPtr<ID3D11Buffer>,8> gc_count_readback_;
    ComPtr<ID3D11RasterizerState> gc_raster_;ComPtr<ID3D11DepthStencilState> gc_depth_;ComPtr<ID3D11BlendState> gc_blend_;
    ComPtr<ID3D11SamplerState> gc_clamp_,gc_wrap_;
    std::array<std::array<UINT,5>,kGroundCoverMeshes> gc_args_template_{};
    std::array<UINT,kGroundCoverMeshes> gc_mesh_first_index_{},gc_mesh_index_count_{};
    // Per placement pass and mesh: its pool region (base, slots). Every pass
    // keeps its own instances until the next frame, so the shadow pass draws
    // each pass's casters in place.
    std::vector<std::array<UINT,kGroundCoverMeshes>> gc_capacity_,gc_base_;
    UINT gc_pass_count_=0;   // passes the current partition holds (the active map and every neighbour map)
    UINT gc_count_passes_=0; // passes the count buffer and its readbacks hold
    std::weak_ptr<const OwnedMapSnapshot> gc_map_;
    std::vector<std::uint8_t> gc_material_state_;          // per atlas material: 0 unused, 1 unpainted, 2 painted
    std::vector<std::array<float,4>> gc_unit_input_;       // per-frame unit list (reused capacity)
    std::vector<ground_cover::FootprintSplat> gc_footprint_input_; // game-visible tracks, reused capacity
    std::uint64_t gc_map_generation_=0,gc_paint_retry_frame_=0,gc_drawn_frame_=UINT64_MAX,gc_painted_frame_=0;
    UINT gc_instance_capacity_=0,gc_readback_head_=0,gc_readback_pending_=0;
    UINT gc_footprint_capacity_=0,gc_trail_width_=0,gc_trail_height_=0,gc_trail_index_=0;
    int gc_trail_origin_x_=0,gc_trail_origin_y_=0;  // window origin of gc_trail_[gc_trail_index_], cells
    float gc_trail_recovery_=0;                      // game seconds until the trail is fully clear; 0: clear
    double gc_trail_seconds_=-1;                     // game clock of the previous cover frame
    int gc_level_=-1;bool gc_failed_=false,gc_grow_=false;
    // Snow trails (snow_trails.h): ping-pong R16G16 field around the view,
    // advanced by the cover pass. Terrain reads the state current when its
    // frame values were written (snow_terrain_srv_ / its window), so a later
    // cover update this frame never misregisters the texture it draws with.
    ComPtr<ID3D11VertexShader> gc_snow_stamp_vs_,gc_snow_tile_vs_;ComPtr<ID3D11PixelShader> gc_snow_stamp_ps_,gc_snow_recover_ps_;
    std::array<ComPtr<ID3D11Texture2D>,2> gc_snow_;
    std::array<ComPtr<ID3D11RenderTargetView>,2> gc_snow_rtv_;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> gc_snow_srv_;
    ComPtr<ID3D11Buffer> gc_snow_segments_;ComPtr<ID3D11ShaderResourceView> gc_snow_segment_srv_;
    UINT gc_snow_segment_capacity_=0,gc_snow_index_=0;
    bool gc_snow_valid_=false;                        // gc_snow_[gc_snow_index_] holds a field at gc_snow_origin_
    std::array<int,2> gc_snow_origin_{};              // texels of kTexel from the world origin
    double gc_snow_seconds_=-1;std::uint64_t gc_snow_map_generation_=0;
    bool gc_snow_map_=false;                          // the current map has snow ground (area class 4)
    bool gc_soft_sand_=false;                         // its tile families include soft sand (terrain_tile_soft_sand)
    struct SnowTrack{std::uint64_t id=0;float x=0,y=0;std::uint64_t frame=0;float along=0;float hx=0,hy=0,hal=0;}; // along: path metres walked; h*: last history point
    // Trail history (snow_trails.h): each walking unit's path in 0.4 m steps with
    // its game second, so a trail the moving window dropped comes back (recovered
    // by its age) when the window returns over it; pruned once fully filled in.
    struct SnowTrailHistory{snow_trails::Segment segment;double seconds=0;};
    std::vector<SnowTrailHistory> gc_snow_history_;
    std::array<float,2> gc_snow_centre_{};            // the window's smooth centre (trail fade), paired with gc_snow_origin_
    std::vector<SnowTrack> gc_snow_tracks_;           // each walking unit's previous position, by lifecycle id
    std::vector<GroundUnitTrail> gc_unit_trails_;     // trail key and body size of gc_unit_input_ rows
    std::vector<FootprintTrailPoint> gc_snow_prints_; // visible prints of walking makers (snow trails of unseen units)
    std::vector<snow_trails::Segment> gc_snow_segment_input_;
    ComPtr<ID3D11ShaderResourceView> snow_terrain_srv_;float snow_terrain_window_[4]{};std::uint32_t snow_terrain_tiles_[4]{};
    D3D12TextureRef snow_terrain12_;                  // D3D12: the same state's field (gc12_.snow), paired as snow_terrain_srv_
    snow_trails::ActiveTiles gc_snow_tiles_;          // per 8 m tile: active until (game seconds)
    std::array<std::uint32_t,4> gc_snow_active_{};    // active tile bits of gc_snow_[gc_snow_index_]
    unsigned gc_snow_full_writes_=0;                  // frames left that write every tile (after a move or restart)
    // One cover frame's snow trail state (advance_snow_trails, both backends):
    // update (the field is advanced and read this frame), window origin,
    // active and written tiles, smooth centre, and the gcSnowWindow/gcSnowStep values.
    struct SnowTrailFrame{bool update=false;std::array<int,2> origin{};std::array<std::uint32_t,4> tiles{},write{};
        std::array<float,2> centre{};float window[4]{},step[4]{};};
    SnowTrailFrame advance_snow_trails(double seconds,const float eye[3],const float inverse[16],float lowest,float highest);
    void gc_shelter_rows(float (&rows)[3][4]) const noexcept;
    // GPU-only per-map contact art; CPU tile payloads are temporary uploads.
    struct ContactBlendMaps {
        ComPtr<ID3D11ShaderResourceView> tiles,atlas;
        D3D12TextureRef tiles12,atlas12;  // renderer_backend=d3d12: the grid and the atlas array
        // D3D11: texture is its terrain 2.0 pool's atlas array (terrain2_pool_range),
        // atlas the view of its slices [base, base + slices) there; pool -1 none.
        ComPtr<ID3D11Texture2D> texture;
        std::uint64_t ready=0,pending=0;
        UINT width=0,height=0,mips=0,slices=0; // slices: the atlas array's (both backends)
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        int pool=-1;UINT base=0;bool pool_refused=false;
        std::uint64_t tiles_serial=0; // D3D11: a new number each time tiles is made (the stacked grid's key)
        // Seam margin (update_terrain_tile_blend_seams): the adjoining maps'
        // vertex cells beyond its shared edges and the sides that have them,
        // as last uploaded.
        std::vector<std::uint16_t> seam_margin;std::uint32_t seam_margin_sides=0;
        // The family cells of its blend rows (family id -> cell) and the
        // families that borrow another family's art.
        std::map<std::uint32_t,std::uint32_t> cells;
        std::set<std::uint32_t> borrowed;
        bool rows_own=false; // its rows were built with own edge families (terrain 2.0)
    };
    ContactBlendMaps contact_maps_;
    HRESULT build_contact_tiles(std::vector<std::array<std::uint32_t,4>> tiles,std::uint64_t used,UINT tx,UINT ty,
        ContactBlendMaps& maps,std::vector<std::array<std::uint32_t,4>>* rows=nullptr);
    HRESULT build_contact_tiles(const TerrainCpuAsset& terrain,UINT tx,UINT ty,ContactBlendMaps& maps,
        std::vector<std::array<std::uint32_t,4>>* rows=nullptr);
    HRESULT copy_contact_atlas(ID3D11Texture2D* source,UINT slice,UINT material,ContactBlendMaps& maps,
        char* detail=nullptr,std::size_t detail_size=0);
    ComPtr<ID3D11SamplerState> contact_sampler_;
    std::uint64_t contact_map_generation_=0,contact_constants_generation_=UINT64_MAX,contact_retry_frame_=0,contact_update_frame_=UINT64_MAX;
    bool contact_incompatible_logged_=false;
    // Terrain material blend (terrain_tile_blend.h): the map's art rows kept
    // for the grid's second half and whether the blend rows are in.
    std::vector<std::array<std::uint32_t,4>> contact_tile_rows_;UINT contact_tiles_x_=0,contact_tiles_y_=0;
    // ready: this map's families were tried; rows: the grid holds blend rows.
    bool tile_blend_ready_=false,tile_blend_rows_=false;float tile_blend_margin_=0;
    HRESULT apply_terrain_tile_blend(const TerrainTileFamilies& families) noexcept;
    // Terrain 2.0 (terrain2.h). Per map (the active map's terrain2_map_,
    // each neighbour's GcNeighbour::terrain2): the material heights parallel
    // to its contact atlas, the material cells to bake (key: slice, slot and
    // fold quadrant) and those baked, and its cell styles (style_serial: a
    // new number every update). own_edges: every map's rows keep its own edge
    // families (terrain2 on). Per frame: the map table (contact map of each
    // entry, set_native_terrain_frame; 0 the active map) and the styles of
    // its entries in one buffer (which serial each entry holds). Per
    // session: the edge mask array (styles with masks as bits) and the height
    // bake's shaders.
    struct Terrain2BakeCell{std::uint32_t key,slice,pos,quadrant,style;bool folded;};
    struct Terrain2Map{
        // D3D11: views of its contact atlas's slice range in the pool's parallel R8 array.
        ComPtr<ID3D11ShaderResourceView> height_srv;ComPtr<ID3D11UnorderedAccessView> height_uav;
        D3D12TextureRef height12;bool mips12=false; // renderer_backend=d3d12: the same R8 array (a storage texture); its levels are due
        std::vector<Terrain2BakeCell> bake_cells;std::set<std::uint32_t> baked;
        std::uint64_t seen_ready=0;bool dirty=false,failed=false;
        std::array<std::uint16_t,4096> styles{};std::uint64_t style_serial=0;
    };
    static constexpr unsigned kTerrain2TableMaps=9,kTerrain2Records=4,kTerrain2Pools=4;
    // D3D11 terrain 2.0 pools (terrain2_pool_range): the contact atlases of
    // one format, size and level count as slice ranges of one array, and
    // their material heights as the same ranges of a parallel R8 array, so
    // the pixel stage indexes maps instead of switching between slots. A
    // range belongs to the ContactBlendMaps that names it (pool, base,
    // slices); the pool keeps no list of its own. Grown only when a map's
    // atlas is made (map loads), never shrunk while in use.
    struct Terrain2Pool{
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT width=0,height=0,mips=0,slices=0;
        ComPtr<ID3D11Texture2D> atlas,heights;ComPtr<ID3D11ShaderResourceView> atlas_srv,heights_srv;
    };
    std::array<Terrain2Pool,kTerrain2Pools> terrain2_pools_{};
    std::uint64_t terrain2_tiles_serials_=0;
    HRESULT terrain2_pool_range(ContactBlendMaps& maps,const D3D11_TEXTURE2D_DESC& atlas,const char* name) noexcept;
    HRESULT terrain2_range_views(const Terrain2Pool& pool,ContactBlendMaps& maps) noexcept;
    HRESULT terrain2_height_views(const Terrain2Pool& pool,const ContactBlendMaps& maps,Terrain2Map& heights) noexcept;
    void trim_terrain2_pools() noexcept;
    // D3D11: every table entry's grid in one array (t85), rebuilt when the
    // entries or their grids change (key: their tiles serials).
    ComPtr<ID3D11Texture2D> terrain2_grids_;ComPtr<ID3D11ShaderResourceView> terrain2_grids_srv_;
    std::array<std::uint64_t,kTerrain2TableMaps> terrain2_grids_key_{};
    // D3D12: the table records' first float in native_terrain_frame_d3d12_
    // and their record 3 (bindless indices) as last uploaded.
    unsigned terrain2_records_first_=0;
    bool terrain2_records_d3d12(float* records) noexcept;
    Terrain2Map terrain2_map_;std::uint64_t terrain2_style_serials_=0;
    std::array<UINT,kTerrain2TableMaps> terrain2_table_{};unsigned terrain2_table_count_=0;
    ComPtr<ID3D11Buffer> terrain2_style_buffer_;ComPtr<ID3D11ShaderResourceView> terrain2_style_srv_;
    std::array<std::uint64_t,kTerrain2TableMaps> terrain2_style_uploaded_{};
    bool tile_blend_own_edges_=false,terrain2_marker_logged_=false,terrain2_table_full_logged_=false;
    ComPtr<ID3D11ShaderResourceView> terrain2_masks_srv_;ComPtr<ID3D11SamplerState> terrain2_mask_sampler_;
    std::uint32_t terrain2_mask_bits_=0;bool terrain2_masks_tried_=false;
    // D3D12: the height bake's programs, and this frame's map table (each
    // entry's grid, atlas and heights; flush_native_d3d12 binds them).
    std::vector<ShaderProgram> terrain2_bake12_programs_;
    std::array<D3D12TextureRef,3*kTerrain2TableMaps> terrain2_table12_{};
    void bake_terrain2_heights_d3d12(Terrain2Map& map,const ContactBlendMaps& contact,const char* name) noexcept;
    void bind_terrain2_d3d12() noexcept;
    ComPtr<ID3D11ComputeShader> terrain2_bake_cs_[6];ComPtr<ID3D11Buffer> terrain2_bake_constants_;
    ComPtr<ID3D11SamplerState> terrain2_bake_sampler_;
    void bake_terrain2_heights(Terrain2Map& map,const ContactBlendMaps& contact,const char* name) noexcept;
    // The snow surface (terrain2.h kSnowSurfaceShader): the active map's snow
    // material cell copied into one tiling, mipmapped texture once its height
    // is baked (key: the map generation and the cell); none without a snow
    // cell (object build-up then keeps its drift relief only).
    std::uint64_t snow_surface_key_=0;
    ComPtr<ID3D11ShaderResourceView> snow_surface_srv_;ComPtr<ID3D11ComputeShader> snow_surface_cs_;
    ComPtr<ID3D11Buffer> snow_surface_constants_;ComPtr<ID3D11SamplerState> snow_surface_sampler_;
    D3D12TextureRef snow_surface12_;std::vector<ShaderProgram> snow_surface12_program_;
    const Terrain2BakeCell* snow_surface_cell(std::uint64_t& key) const noexcept;
    void update_snow_surface() noexcept;
    void update_snow_surface_d3d12() noexcept;
    void load_terrain2_masks() noexcept;
    void update_terrain2_table(bool on,float* records) noexcept;
    // The terrain 2.0 table entry + 1 of contact map id (0: the active map), 0 when 2.0 does not draw it.
    UINT terrain2_table_entry(UINT contact_map) const noexcept{
        for(unsigned k=0;k<terrain2_table_count_;++k)if(terrain2_table_[k]==contact_map)return k+1;
        return 0;
    }
    // Terrain 2.0 material cache (terrain2_cache.h; renderer_terrain2_cache.cpp).
    struct Terrain2CacheLevelState{
        bool active=false,fine=false;int slice=-1;
        Terrain2CacheRect window,valid,bakeable;std::vector<Terrain2CacheRect> pending;
        int atlas[2]{};
    };
    std::array<Terrain2CacheLevelState,kTerrain2CacheLevels> terrain2_cache_levels_{};
    // The inputs terrain2Color reads (set_native_terrain_frame copies the frame
    // values; rows and height bakes bump the revision), hashed each frame.
    std::array<float,4+8+8+4+4*kTerrain2Records*kTerrain2TableMaps> terrain2_cache_frame_values_{};
    std::uint64_t terrain2_cache_revision_=0,terrain2_cache_inputs_=0;
    ComPtr<ID3D11Texture2D> terrain2_cache_fine_,terrain2_cache_coarse_;
    ComPtr<ID3D11ShaderResourceView> terrain2_cache_fine_srv_,terrain2_cache_coarse_srv_;
    std::vector<ComPtr<ID3D11RenderTargetView>> terrain2_cache_fine_rtvs_;ComPtr<ID3D11RenderTargetView> terrain2_cache_coarse_rtv_;
    ComPtr<ID3D11Buffer> terrain2_cache_params_,terrain2_cache_bake_constants_;ComPtr<ID3D11ShaderResourceView> terrain2_cache_params_srv_;
    ComPtr<ID3D11SamplerState> terrain2_cache_sampler_;ComPtr<ID3D11RasterizerState> terrain2_cache_raster_;
    ComPtr<ID3D11VertexShader> terrain2_cache_vs_;ComPtr<ID3D11PixelShader> terrain2_cache_ps_;
    // D3D12: the fine array and coarse atlas as storage textures, the params
    // buffer, and the compute bake (nativeTerrain2CacheCS).
    // S3: the R8 height layer beside the colour levels (same slices and atlas; t78/t79).
    ComPtr<ID3D11Texture2D> terrain2_cache_height_fine_,terrain2_cache_height_coarse_;
    ComPtr<ID3D11ShaderResourceView> terrain2_cache_height_fine_srv_,terrain2_cache_height_coarse_srv_;
    std::vector<ComPtr<ID3D11RenderTargetView>> terrain2_cache_height_fine_rtvs_;ComPtr<ID3D11RenderTargetView> terrain2_cache_height_coarse_rtv_;
    D3D12TextureRef terrain2_cache_fine12_,terrain2_cache_coarse12_;D3D12BufferRef terrain2_cache_params12_;
    D3D12TextureRef terrain2_cache_height_fine12_,terrain2_cache_height_coarse12_;
    std::vector<ShaderProgram> terrain2_cache_programs12_;
    int terrain2_cache_side_=0,terrain2_cache_slices_=0,terrain2_cache_coarse_size_[2]{};
    std::uint64_t terrain2_cache_frame_=UINT64_MAX,terrain2_cache_map_=0,terrain2_cache_fill_texels_=0,terrain2_cache_fill_frames_=0;
    bool terrain2_cache_failed_=false,terrain2_cache_on_=false,terrain2_cache_unsettled_logged_=false;unsigned terrain2_cache_unsettled_=0;
    HRESULT ensure_terrain2_cache_programs() noexcept;
    void update_terrain2_cache() noexcept;
    void bind_terrain2_cache() noexcept;
    void release_terrain2_cache() noexcept;
    bool bake_terrain2_cache_rect(unsigned level,const Terrain2CacheRect& rect) noexcept; // false: not baked (D3D12 pipeline not ready or dropped)
    void update_terrain2_styles(Terrain2Map& map,const TerrainTileFamilies& families,const std::map<std::uint32_t,std::uint32_t>& cells,
        const std::vector<std::array<std::uint32_t,4>>& rows,const std::vector<std::uint32_t>& fold_table,
        const std::vector<std::array<std::uint32_t,4>>& art) noexcept;
    // Shared map edges (update_terrain_tile_blend_seams): the active map's
    // blend inputs and own vertex families kept for rebuilding its rows when
    // a neighbour's families arrive, the families it can draw (by name) and
    // the edge families its rows hold.
    std::shared_ptr<const TerrainTileBlendInput> tile_blend_input_;
    std::vector<std::uint32_t> tile_blend_vertex_,tile_blend_edge_;
    std::set<std::string> tile_blend_drawable_;
    // On every vertex two or more loaded maps share, each map's blend rows
    // use the vertex family of the first map by layout name whose family
    // every map there can draw, matched by editor family name, so the seam
    // is the same whichever map is active. Without a common family the maps
    // keep their own there (logged once per map and family name).
    // Rebuilds the rows of every map whose edge families changed.
    void update_terrain_tile_blend_seams() noexcept;
    std::set<std::string> tile_blend_seam_unmatched_; // map/family pairs reported once
    // The active map's tile families, loaded off the render thread (one job at
    // a time) for ambient_map_generation_ while the material blend or ground
    // cover is on. poll_terrain_tile_families: true once tile_families_ is in.
    TileFamiliesJob tile_families_job_;
    // Started as soon as a new main terrain source is published (inside the
    // game's load), adopted by the first poll for that same terrain.
    TileFamiliesJob tile_families_prestart_;
    std::shared_ptr<const TerrainCpuAsset> tile_families_prestart_terrain_;
    void prestart_terrain_tile_families() noexcept;
    std::uint64_t tile_families_job_generation_=UINT64_MAX,tile_families_generation_=UINT64_MAX;
    TerrainTileFamilies tile_families_;
    bool poll_terrain_tile_families() noexcept;
    // Ground cover's base types carry the sand-family bit for this map generation.
    std::uint64_t gc_sand_generation_=UINT64_MAX;

    // Cover drawing nothing for a missing input: the reason, since when it has
    // held, and whether its one line was logged. The colour map stays
    // unpainted (no cover anywhere) until one terrain atlas is readable.
    const char* gc_skip_reason_=nullptr;ULONGLONG gc_skip_since_ms_=0;std::uint64_t gc_skip_frame_=UINT64_MAX;
    bool gc_skip_reported_=false,gc_color_unpainted_=true;const char* gc_paint_failure_=nullptr;
    HRESULT build_ground_cover_surfaces(ID3D11ShaderResourceView* area,ID3D11ShaderResourceView* height,
        ID3D11ShaderResourceView* ground,ID3D11ShaderResourceView* type,ID3D11ShaderResourceView* sea,UINT tiles_x,UINT tiles_y,
        UINT color_texels,ComPtr<ID3D11ShaderResourceView>& fields_srv,ComPtr<ID3D11Texture2D>& color,
        ComPtr<ID3D11ShaderResourceView>& color_srv,ComPtr<ID3D11UnorderedAccessView>& color_uav) noexcept;
    HRESULT dispatch_ground_cover_paint(ID3D11ShaderResourceView* tiles,ID3D11ShaderResourceView* atlas,
        ID3D11UnorderedAccessView* color,std::uint32_t material,UINT tiles_x,UINT tiles_y,UINT texels) noexcept;
    // Neighbour-map cover: per uploaded neighbour, its own immutable grids on
    // the GPU (no CPU copy kept), placed in its map metres at its world origin
    // after the active map's cover, through the same pool. Released with the
    // neighbours or the cover.
    // D3D12: one map's cover inputs, the active map's (gc12_.map) and each
    // neighbour's (GcNeighbour::cover12) alike. The same placement, paint
    // and draws run over every map with its own grids and frame parameters.
    struct GcMapD3D12 {
        D3D12TextureRef area,height,ground,type,sea,trees,fields,color,bark,stone,masks;
        D3D12BufferRef tiles;
        // Its terrain atlas pages per material (a neighbour's, held until
        // painted); empty: the active map's drawn atlases (bridge_terrain_atlas).
        std::vector<D3D12TextureRef> atlases;
        std::vector<std::uint8_t> paint;
        std::vector<std::uint32_t> tile_classes;
        std::vector<std::uint64_t> cell_masks;std::uint64_t masks_key=UINT64_MAX;
        UINT width=0,height_tiles=0,color_texels=4;
        std::uint64_t generation=UINT64_MAX;  // the active map: ambient_map_generation_ of its grids (contact colour, the bounce light's ground colour)
        bool fields_ready=false,color_ready=false;
    };
    struct GcD3D12 {
        std::vector<ShaderProgram> programs;
        D3D12BufferRef vertices,indices,rules,rule_class,cell_jobs,cell_counts,rounds;
        D3D12BufferRef candidates,survivors,stage_counts[2],work,args,counts,overflow,instances,shade,draw_inputs,zones;
        D3D12BufferRef objects,footprints,splat_args;
        D3D12TextureRef object_mask,trail[2],trail_sources;
        // Snow trails (as gc_snow_*): the ping-pong field and this frame's segments.
        D3D12TextureRef snow[2];D3D12BufferRef snow_segments;
        // Sun casters (as gc_caster_*): per pass, close map and mesh draw
        // arguments, and the slot lists (close map s at s * pool_capacity);
        // per pass, the frame constants and ground its placement drew with
        // (empty: that pass casts nothing next frame).
        D3D12BufferRef caster_args,caster_slots;UINT caster_passes=0;
        std::vector<std::array<UINT,5>> caster_template; // the arguments with every list count zero
        struct CasterPass{std::vector<std::uint8_t> frame;D3D12TextureRef ground;};
        std::vector<CasterPass> casters;
        std::array<std::array<std::uint32_t,68>,kGcPlaceClasses> classes{};
        GcMapD3D12 map;
        std::uint64_t map_key=UINT64_MAX;
        UINT candidate_capacity=0,cell_capacity=0,pool_capacity=0;
        UINT passes=0; // pool regions (maps) the counts and overflow buffers hold
        std::array<std::uint64_t,8> readbacks{};
        UINT read_head=0,read_pending=0;
        UINT demand_state=0;std::uint64_t demand_frame=0;
    } gc12_;
    HRESULT ensure_ground_cover_d3d12();
    HRESULT build_ground_cover_map_d3d12(GcMapD3D12& map,UINT tiles_x,UINT tiles_y,UINT color_texels,
        const std::vector<std::uint8_t>& area,const std::vector<float>& height,const std::vector<float>& ground,
        const std::vector<std::uint16_t>& types,const std::vector<std::uint8_t>& base_types,const std::vector<GcTileGpu>& tiles,
        const std::vector<std::uint8_t>& used,const std::vector<std::array<std::uint8_t,4>>& trees);
    HRESULT prepare_ground_cover_map_d3d12(GcMapD3D12& map);
    // The cover map whose painted colour and ground the contact blend of a
    // batch with this contact map reads on D3D12 (0: the active map); null
    // while it is unpainted or the cover is off.
    const GcMapD3D12* contact_cover_map_d3d12(UINT contact_map) const noexcept;
    struct GcNeighbour {
        UINT contact_map=0;
        std::vector<std::uint32_t> tile_classes;                // its tile classes (gc_tile_classes_)
        std::vector<std::uint64_t> masks;std::uint64_t masks_key=UINT64_MAX; // its per-cell rule masks
        ComPtr<ID3D11ShaderResourceView> mask_srv;
        UINT type_epoch=0;                                    // counts replacements of type (tile families applied)
        ContactBlendMaps contact;
        std::string name;
        ComPtr<ID3D11ShaderResourceView> area,height,ground,type,trees,fields,color;
        ComPtr<ID3D11Texture2D> color_texture;
        // Until prepared (first cover frame with the pipeline ready): sea mask,
        // tile atlas cells and the neighbour terrain's own atlas pages.
        ComPtr<ID3D11ShaderResourceView> sea,tiles;
        std::vector<ComPtr<ID3D11ShaderResourceView>> atlases;
        std::array<float,3> origin{};
        // Its saved-layout origin snapped to whole tiles (neighbour_layout_tile_origin):
        // the frame of its terrain material layouts, the same as when it is the active map.
        std::array<float,2> layout_origin{};
        float lowest=0,highest=0;
        std::array<float,4> fog_band{}; // world border-fog band of the piece (its fog_edges)
        UINT tiles_x=0,tiles_y=0;
        std::uint64_t bytes=0;
        bool prepared=false;
        // Until its tile families load: base tile slots and slot types for the
        // sand-family bit; beach rules see ETERRTYPE sand only meanwhile.
        TileFamiliesJob families;
        std::vector<std::uint16_t> base_slots;std::vector<std::uint32_t> slot_types;std::vector<std::uint8_t> base_lanes;
        // Terrain material blend of this neighbour's own terrain (its own
        // families and atlas; terrain_tile_blend.h): the contact grid's rows
        // and the family-independent blend inputs, kept with its families and
        // own vertex families for rebuilding its rows on a shared edge change
        // (update_terrain_tile_blend_seams); blend_edge: the edge families
        // its rows hold. contact.tiles always has the blend half (rows
        // [ty,2ty), no material until the families load), as the active
        // map's grid does.
        std::vector<std::array<std::uint32_t,4>> contact_rows;
        std::shared_ptr<TerrainTileBlendInput> blend_input;
        std::shared_ptr<const TerrainTileFamilies> blend_families;
        std::vector<std::uint32_t> blend_vertex,blend_edge;
        std::set<std::string> blend_drawable;
        bool blend_rows=false;
        Terrain2Map terrain2; // its terrain 2.0 heights and styles
        GcMapD3D12 cover12; // D3D12: its cover map (built at adoption)
    };
    std::vector<GcNeighbour> gc_neighbours_;
    void add_ground_cover_neighbour(const NeighbourPiece& upload,const std::array<float,16>& active_to_world,UINT contact_map) noexcept;
    // Once a neighbour's tile families load: its beach-sand cover types and
    // its terrain material blend rows.
    void apply_neighbour_tile_families(GcNeighbour& neighbour) noexcept;
    HRESULT prepare_ground_cover_neighbour(GcNeighbour& neighbour) noexcept;
    bool terrain_full_sun_=false; // terrain lighting compares the full sun (set with the terrain frame)
    std::future<std::vector<ComPtr<ID3DBlob>>> gc_compile_;   // worker shader compile
    std::shared_ptr<std::atomic<std::size_t>> gc_compile_done_; std::size_t gc_compile_total_ = 0; // its programs done (load progress)
    // F10 line: what the last particle pass did, or why it drew nothing.
    char ambient_status_[256]="Particles: pass not reached yet";bool ambient_status_problem_=false;
    std::uint64_t ambient_status_frame_=UINT64_MAX;
    // Wind trails (renderer_wind_trails.cpp): thin gust streaks near the
    // camera in windy weather, after the ambient particles and before SMAA.
    // Level 0 creates nothing.
    void render_wind_trails();
    ComPtr<ID3D11VertexShader> wind_trails_vs_;ComPtr<ID3D11PixelShader> wind_trails_ps_;
    ComPtr<ID3D11Buffer> wind_trails_constants_;ComPtr<ID3D11BlendState> wind_trails_blend_;
    ComPtr<ID3D11RasterizerState> wind_trails_raster_;
    std::array<wind_trails::Slot,wind_trails::kMaxSlots> wind_trails_slots_{};
    double wind_trails_time_=-1;std::uint64_t wind_trails_generation_=0;int wind_trails_level_=-1;bool wind_trails_failed_=false;
    // Owned precipitation (renderer_precipitation.cpp): rain and snow that
    // replace the game's CWeather draws, after the wind trails and before
    // SMAA. Quality 0 creates nothing (and the stock draws stay suppressed).
    void render_precipitation();
    ComPtr<ID3D11VertexShader> precipitation_vs_,precipitation_veil_vs_,precipitation_splash_vs_;
    ComPtr<ID3D11PixelShader> precipitation_ps_,precipitation_veil_ps_,precipitation_splash_ps_;
    ComPtr<ID3D11Buffer> precipitation_constants_;ComPtr<ID3D11BlendState> precipitation_blend_;ComPtr<ID3D11RasterizerState> precipitation_raster_;
    bool precipitation_failed_=false;
    char precipitation_status_[256]="Precipitation: pass not reached yet";bool precipitation_status_problem_=false;
    std::uint64_t precipitation_status_frame_=UINT64_MAX;
    char precipitation_status_state_[96]="";
    unsigned long long precipitation_cover_log_=0;
    float precipitation_focus_smooth_=0;unsigned long long precipitation_focus_tick_=0;  // eased focus distance (real time)  // last precipitation_cover bridge event (tick ms)  // the status line's state last reported to the bridge log
    RainOcclusionRequest precipitation_request_{};  // the shelter map wanted (bridge bakes it)
    // The game's weather atlas (snow crystals) and its sampler; reason on failure.
    const char* load_precipitation_flakes() noexcept;
    ComPtr<ID3D11ShaderResourceView> precipitation_flakes_srv_;ComPtr<ID3D11SamplerState> precipitation_sampler_;
    bool precipitation_flakes_failed_=false;
    unsigned long long native_rain_bakes_=0;
    // Precipitation build-up/fade and the falling field's wind drift (game seconds).
    float precipitation_level_=0;int precipitation_kind_=0;double precipitation_time_=-1;std::uint64_t precipitation_ramp_generation_=0;
    float precipitation_rain_visible_=0;  // ramped rain x strength share: rain rings and wet ripples
    float precipitation_snow_visible_=0;  // the same for snow: snow build-up and trail refill
    float precipitation_strength_seen_=-1;  // the strength the picture shows (cross-fades, never from zero)
    double precipitation_onset_=-1;
    bool precipitation_paused_=false;int precipitation_wanted_=0;  // game clock still this frame; the kind it waits to build        // game seconds the current rain began (drizzle, then the front)
    float precipitation_local_cover_=-1;           // the sky's scattered cover overhead, 0..1 (renderer_sky; -1: not published)
    float precipitation_front_arrival_=-1;           // the sky's storm front over the eye, 0..1 (renderer_sky; -1: not published)
    float snow_cover_=0;double snow_cover_hours_=-1;std::uint64_t snow_cover_generation_=UINT64_MAX;
    float shelter_fill_=0;  // how far snow has crept under crowns (slower than snow_cover_, keeps growing after it is full)  // snow build-up on surfaces and its game seconds (precipitation clock)
    double precipitation_drift_[2]{};float precipitation_wind_[2]{};double precipitation_drift_time_=-1;std::uint64_t precipitation_drift_generation_=0;  // shelter bakes so far (F10 line: it must stay still while nothing changes)
    float wet_level_=0;double wet_hours_=-1;std::uint64_t wet_generation_=UINT64_MAX;  // wet surfaces: lingering rain wetness and its game hours
    // Shelter map (renderer_precipitation.cpp): one straight-down depth map of
    // the persistent static casters, drawn through flush_native_shadows.
    bool native_rain_active_=false,native_rain_valid_=false;UINT native_rain_side_=0;NativeShadowSlice native_rain_slice_{};
    ComPtr<ID3D11Texture2D> native_rain_depth_;ComPtr<ID3D11DepthStencilView> native_rain_dsv_;
    ComPtr<ID3D11ShaderResourceView> native_rain_srv_;ComPtr<ID3D11Buffer> native_rain_constants_;
    // Its foliage mask (R8, the same side): 1 where the top caster is foliage.
    ComPtr<ID3D11Texture2D> native_rain_foliage_;ComPtr<ID3D11RenderTargetView> native_rain_foliage_rtv_;ComPtr<ID3D11ShaderResourceView> native_rain_foliage_srv_;
    // Its soft version for the snow and wet surfaces (filter_native_rain_shelter):
    // ping-pong RGBA16 maps; the eighth pass writes map 1, the soft map, and
    // the blur (shelterBlurCS) writes map 0, the cover the pixel shaders read
    // (re-run when shelter_fill_ moves by 1/256 from native_shelter_blur_fill_).
    ComPtr<ID3D11Texture2D> native_shelter_soft_[2];ComPtr<ID3D11ShaderResourceView> native_shelter_soft_srv_[2];
    ComPtr<ID3D11UnorderedAccessView> native_shelter_soft_uav_[2];ComPtr<ID3D11ComputeShader> native_shelter_soft_cs_;
    ComPtr<ID3D11Buffer> native_shelter_soft_constants_;UINT native_shelter_soft_side_=0;
    ComPtr<ID3D11ComputeShader> native_shelter_blur_cs_;float native_shelter_blur_fill_=-1;
    // The rain ring texture for wet ground (update_native_rain_rings, t71).
    ComPtr<ID3D11Texture2D> native_rain_rings_;ComPtr<ID3D11ShaderResourceView> native_rain_rings_srv_;ComPtr<ID3D11UnorderedAccessView> native_rain_rings_uav_;
    ComPtr<ID3D11ComputeShader> native_rain_rings_cs_;ComPtr<ID3D11Buffer> native_rain_rings_constants_;
    float native_rain_rings_key_[2]{-1.f,-1.f};bool native_rain_rings_failed_=false;
    bool native_rain_rings_bound_=false; // this terrain frame reads the ring texture (t71)
    bool native_shelter_soft_valid_=false,native_shelter_soft_failed_=false;
    // D3D12 backend: the shelter map (one D32 slice, read as Texture2D), the
    // bake's shadow constants (b2) and the soft maps' ping-pong pair.
    struct RainD3D12 {
        D3D12TextureRef shelter;
        D3D12TextureRef foliage;  // the bake's foliage mask (R8 colour target beside the slice, filter t2)
        std::array<D3D12TextureRef,2> soft;
        D3D12TextureRef rings;  // the rain ring texture (update_native_rain_rings_d3d12, t71)
        NativeShadowConstants constants{};
    } rain12_;

    // Critters (renderer_critters.cpp): ambient creatures in the opaque world
    // pass after the ground cover. Level 0 creates and reads nothing.
    HRESULT render_critters(const FixedState& frame,const std::array<float,16>& world_to_clip) noexcept;
    struct CritterState;
    std::shared_ptr<CritterState> critters_;
    char critters_status_[1024]="Critters: pass not reached yet";
    std::uint64_t critters_status_frame_=UINT64_MAX;
    CritterSummary critters_summary_{};   // director state behind critters_status_
    bool critters_failed_=false;
    std::uint64_t critters_drawn_frame_=UINT64_MAX;
    // Fur prototype (renderer_fur.cpp, fur.h; F10 fur_mode 1 shells, 2
    // strands): drawn inside flush_native after its colour batches. Off
    // creates and binds nothing.
    void draw_fur_records() noexcept;
    void release_fur() noexcept;
    struct FurState;
    std::shared_ptr<FurState> fur_;
    bool fur_failed_=false;
    ComPtr<ID3D11SamplerState> water_reflection_sampler_;
    std::array<float,16> water_capture_clip_{},water_capture_inverse_{};
    std::uint64_t water_capture_frame_ = UINT64_MAX;
    // The flush whose first liquid batch took the snapshot (native_flush_serial_
    // counts flushes): a later flush of the same frame retakes it (prepare_water_reflections).
    std::uint64_t water_capture_flush_ = 0, native_flush_serial_ = 0;
    bool water_capture_ready_ = false, water_failure_reported_ = false;
    bool water_late_draw_reported_ = false; // opaque world draw after the water copy (probe event, once)
    std::vector<std::function<HRESULT()>> deferred_ground_marks_; // draw_game_selection_held, draw_game_print_run_held
    bool drawing_ground_marks_ = false;
    HRESULT create_water_reflection_targets();
    // Half-resolution water reflection targets: 0 rays, 1 water points (the
    // liquid input pass), 2 traced colour, 3 key (the march), 4 filtered
    // colour, 5 filtered key (nativeLiquidSsrFilterPS); the input pass's
    // depth. Sized (width+1)/2 x (height+1)/2 of the world target.
    std::array<ComPtr<ID3D11Texture2D>,6> water_ssr_textures_;
    std::array<ComPtr<ID3D11RenderTargetView>,6> water_ssr_rtv_;
    std::array<ComPtr<ID3D11ShaderResourceView>,6> water_ssr_srv_;
    ComPtr<ID3D11Texture2D> water_ssr_depth_;ComPtr<ID3D11DepthStencilView> water_ssr_dsv_;
    ComPtr<ID3D11DepthStencilState> water_ssr_depth_state_;
    UINT water_ssr_width_=0,water_ssr_height_=0;bool water_ssr_failure_reported_=false;
    HRESULT create_water_ssr_targets();
    bool prepare_water_reflections(const FixedState& state);
    void capture_water_history();
    // D3D12 backend (renderer_liquid.cpp): the reflection scene copy (two
    // slices, the history with mips), the pre-water depth snapshot, the
    // half-resolution trace targets and this frame's reflection constants (b7).
    struct WaterD3D12 {
        D3D12TextureRef scene, depth, ssr[6], ssr_depth;
        WaterReflectionConstants constants{};
    } water12_;
    HRESULT create_water_reflection_targets_d3d12();
    HRESULT create_water_ssr_targets_d3d12();
    void clear_water_reflection_bindings() noexcept;
    HRESULT ensure_liquid_normal_map();
    // The map swamp frame's texture (TerrainCpuAsset::swamp_frame_texels),
    // rebuilt per ambient map generation and bound at t33 for liquid draws.
    void bind_liquid_swamp_frame() noexcept;
    // The generated liquid maps, made on a worker from device creation on.
    struct LiquidMapsPrepared {HRESULT hr=E_FAIL;ComPtr<ID3D11ShaderResourceView> normal,detail;std::vector<std::array<float,2>> lava_samples;
        std::vector<std::vector<std::uint32_t>> normal_mips,detail_mips;}; // D3D12: CPU levels (the host uploads them)
    std::future<LiquidMapsPrepared> liquid_maps_job_;
    void start_liquid_maps_job() noexcept;
    std::uint64_t water_liquid_batches_=0,water_reflection_batches_=0,water_reflection_resource_failures_=0; // temporary SSR witness
    ComPtr<ID3D11ShaderResourceView> liquid_normal_srv_,liquid_detail_srv_;ComPtr<ID3D11SamplerState> liquid_normal_sampler_;
    ComPtr<ID3D11ShaderResourceView> liquid_swamp_frame_srv_;std::uint64_t liquid_swamp_frame_generation_=UINT64_MAX;
    // D3D12 backend (renderer_liquid.cpp): the same maps, fields and wave
    // simulation as executor textures, bound for the world draws as D3D11
    // binds them (bind_liquid_d3d12: b8, s6, t22-t24, t33, t62, t63).
    struct LiquidD3D12 {
        D3D12TextureRef normal, detail, swamp_frame, surf, surf_level, wave[2];
        std::vector<ShaderProgram> wave_program;
    } liquid12_;
    HRESULT ensure_liquid_maps_d3d12();
    void bind_liquid_d3d12();
    void reset_water_reflections() noexcept;
    ComPtr<ID3D11ShaderResourceView> debug_stencil_srv_;
    // Ambient occlusion (ambient_occlusion.h): full-resolution ambient share
    // written by world pixels, low-resolution horizon AO, then denoised AO
    // (ao_srv_, also the debug view). ao_depth_ holds one compact depth per AO
    // pixel for the horizon samples. Created at a composite boundary.
    HRESULT update_ambient_occlusion_targets() noexcept;
    HRESULT render_ambient_occlusion() noexcept;
    void release_ambient_occlusion_targets() noexcept;
    ComPtr<ID3D11Texture2D> ao_ambient_texture_,ao_raw_texture_,ao_filtered_texture_,ao_depth_texture_;
    ComPtr<ID3D11RenderTargetView> ao_ambient_rtv_,ao_raw_rtv_,ao_filtered_rtv_,ao_depth_rtv_;
    ComPtr<ID3D11ShaderResourceView> ao_ambient_srv_,ao_raw_srv_,ao_depth_srv_;
    ComPtr<ID3D11ShaderResourceView> ao_srv_;
    ComPtr<ID3D11VertexShader> ao_vs_;
    ComPtr<ID3D11PixelShader> ao_prepare_ps_,ao_horizon_ps_,ao_denoise_ps_,ao_resolve_ps_;
    ComPtr<ID3D11Buffer> ao_constants_;
    ComPtr<ID3D11BlendState> ao_resolve_blend_;
    unsigned ao_divisor_=0;
    // World fog atmosphere (world_fog.h, renderer_fog.cpp).
    HRESULT bind_world_fog() noexcept;
    HRESULT ensure_world_fog_volume(int quality) noexcept;
    HRESULT update_world_fog_mist() noexcept;
    // D3D12 backend (renderer_fog.cpp): the swamp mist texture and the froxel
    // volume as executor textures, the inject/integrate dispatches, and the
    // integrated volume at t11 for the world pixel programs.
    struct FogD3D12 {
        std::vector<ShaderProgram> programs;  // append_fog_volume_shader_programs: inject, integrate
        D3D12TextureRef mist, scatter[2], integrated;
    } fog12_;
    HRESULT update_world_fog_mist_d3d12() noexcept;
    HRESULT ensure_world_fog_volume_d3d12(int quality) noexcept;
    HRESULT dispatch_world_fog_volume_d3d12() noexcept;
    void bind_world_fog_volume_d3d12() noexcept;
    void release_world_fog_volume() noexcept;
    ComPtr<ID3D11Buffer> fog_frame_constants_;
    FogFrameGpu fog_frame_d3d12_{};  // renderer_backend=d3d12: the frame values (b10) the D3D12 flush sets
    ComPtr<ID3D11SamplerState> fog_sampler_;
    ComPtr<ID3D11ComputeShader> fog_inject_cs_,fog_integrate_cs_;
    ComPtr<ID3D11Texture3D> fog_scatter_[2],fog_integrated_;
    ComPtr<ID3D11UnorderedAccessView> fog_scatter_uav_[2],fog_integrated_uav_;
    ComPtr<ID3D11ShaderResourceView> fog_scatter_srv_[2],fog_integrated_srv_,fog_mist_srv_;
    std::array<float,4> fog_mist_grid_{};
    std::array<float,16> fog_previous_clip_{};
    std::uint64_t fog_prepared_frame_=UINT64_MAX,fog_mist_generation_=0;
    double fog_wetness_hours_=-1.0;std::uint64_t fog_world_generation_=UINT64_MAX;
    float fog_wetness_=0.f,fog_rain_level_=0.f,fog_terrain_low_z_=0.f;
    std::array<float,4> fog_previous_eye_{};
    const char* fog_volume_reason_="";
    char fog_status_[512]="Fog: no world frame yet";
    bool fog_status_problem_=false;
    std::string fog_status_state_;
    unsigned fog_scatter_index_=0;int fog_volume_quality_=-1;
    bool fog_history_valid_=false,fog_volume_active_=false,fog_volume_failed_=false;
    // Screen-space sun rays of the current world frame, prepared with the fog.
    std::uint64_t sun_rays_frame_=UINT64_MAX;float sun_rays_center_[2]{},sun_rays_tint_[3]{},sun_rays_haze_luma_=0;
    // Owned sky, volumetric clouds and cloud shadows (sky_clouds.h, renderer_sky.cpp).
    void prepare_owned_sky(FogFrameGpu& fog,const WorldEnvironment& environment,const float eye[3]) noexcept;
    void draw_owned_sky() noexcept;
    HRESULT ensure_owned_sky(int quality) noexcept;
    void release_owned_sky() noexcept;
    void bind_owned_sky_pixel_inputs() noexcept;
    // D3D12 backend (renderer_sky.cpp): the same programs and textures on the
    // executor. S_FALSE: a program is not ready yet (the sky waits, named).
    HRESULT ensure_owned_sky_d3d12(int quality) noexcept;
    HRESULT dispatch_owned_sky_d3d12(const SkyFrameGpu& frame) noexcept;
    void draw_owned_sky_d3d12() noexcept;
    struct SkyD3D12 {
        std::vector<ShaderProgram> programs;  // append_sky_shader_programs order
        D3D12TextureRef shape, detail, weather, lut, shadow, shadow_raw, march, cloud[2], probe[2];
        std::vector<unsigned char> frame;  // this frame's sky constants (skyPS b0)
        bool noise_ready = false, pending = false;
    } sky12_;
    ComPtr<ID3D11ComputeShader> sky_shape_cs_,sky_detail_cs_,sky_weather_cs_,sky_lut_cs_,sky_shadow_cs_,sky_shadow_blur_cs_,sky_clouds_cs_,sky_resolve_cs_,sky_probe_cs_;
    ComPtr<ID3D11VertexShader> sky_vs_;ComPtr<ID3D11PixelShader> sky_ps_;
    ComPtr<ID3D11Buffer> sky_constants_;
    ComPtr<ID3D11SamplerState> sky_wrap_sampler_,sky_clamp_sampler_;
    ComPtr<ID3D11DepthStencilState> sky_depth_state_;ComPtr<ID3D11BlendState> sky_blend_state_;
    ComPtr<ID3D11RasterizerState> sky_raster_state_;
    ComPtr<ID3D11ShaderResourceView> sky_shape_srv_,sky_detail_srv_,sky_weather_srv_,sky_lut_srv_,sky_shadow_srv_;
    ComPtr<ID3D11UnorderedAccessView> sky_lut_uav_,sky_shadow_uav_;
    // The raw cloud-shadow field (skyShadowCS); sky_shadow_srv_ holds it pre-blurred (skyShadowBlurCS).
    ComPtr<ID3D11ShaderResourceView> sky_shadow_raw_srv_;ComPtr<ID3D11UnorderedAccessView> sky_shadow_raw_uav_;
ComPtr<ID3D11ShaderResourceView> sky_march_srv_;ComPtr<ID3D11UnorderedAccessView> sky_march_uav_;
    ComPtr<ID3D11ShaderResourceView> sky_cloud_srv_[2],sky_probe_srv_[2];
    ComPtr<ID3D11UnorderedAccessView> sky_cloud_uav_[2],sky_probe_uav_[2];
    std::array<float,16> sky_previous_clip_{};
    double sky_wind_offset_[3]{};
    double sky_time_=-1.0;
    // The inputs the sky LUT and the cloud shadow field were last computed
    // from (empty: recompute). Equal inputs give equal outputs: skipped.
    std::vector<unsigned char> sky_lut_inputs_,sky_shadow_inputs_;
    double sky_shadow_offset_[2]{};  // slower drift of the cloud-shadow field (sky_cloud_shadow_speed)
    float sky_fair_coverage_=-1.f;
    float sky_closure_=-1.f;  // the storm deck over the eye last frame (sky_front_deck at 0)
    // Storm front edges (m upwind of the eye) and the pre-storm overhead cover
    // (sky_clouds.h SkyFront, sky_step_front).
    float sky_front_lead_=-80000.f,sky_front_rear_=-80000.f,sky_front_pre_=0.f;
public:
    // Published once per world frame for the precipitation ramp: the storm
    // front's arrival over the eye (0..1) and the small pre-storm clouds
    // forming overhead (0..1).
    float sky_front_arrival() const noexcept { return sky_front_arrival_; }
    float sky_local_cover() const noexcept { return sky_local_cover_; }
private:
    float sky_front_arrival_=0.f,sky_local_cover_=0.f;
    float sky_deck_kind_=1.f; // storm weight of the closing weather: rain 1, snow .8 (sky_deck_storm)
    // Latest game lightning strike (sky_strike_light): game seconds and ground xy;
    // the strike lights listed last frame (id, half-unit centre) find new ones.
    double sky_strike_seconds_=-1.0;float sky_strike_xy_[2]{};
    std::vector<std::array<std::int32_t,4>> sky_strike_keys_;
    float sky_fog_color_[3]{.5f,.6f,.7f};  // game fog colour of the current world flush
    unsigned sky_index_=0,sky_cloud_width_=0,sky_cloud_height_=0,sky_march_width_=0,sky_march_height_=0,sky_shadow_size_=0,sky_probe_width_=0;
    int sky_quality_=-1;std::uint32_t sky_frame_=0,sky_jitter_=0;
    bool sky_history_valid_=false,sky_failed_=false,sky_draw_pending_=false,sky_inputs_active_=false;
    const char* sky_reason_="";
    char sky_status_[576]="Sky: no world frame yet";
    bool sky_status_problem_=false;
    std::string sky_status_state_;
    bool ao_ambient_written_=false,ao_pipeline_failed_=false; // a world batch wrote the ambient share this frame
    std::vector<PendingBlit> pending_blits_;
    PendingBlit pending_cursor_;
    bool cursor_pending_ = false;
    ComPtr<ID3D11Texture2D> camp_art_texture_;
    ComPtr<ID3D11ShaderResourceView> camp_art_srv_;
    ComPtr<ID3D11Texture2D> camp_control_texture_;
    ComPtr<ID3D11ShaderResourceView> camp_control_srv_;
    ComPtr<ID3D11Texture2D> camp_construction_texture_;
    ComPtr<ID3D11ShaderResourceView> camp_construction_srv_;
    bool camp_art_attempted_ = false;
    ComPtr<ID3D11Texture2D> camp_popup_color_,camp_popup_depth_;
    ComPtr<ID3D11RenderTargetView> camp_popup_rtv_,camp_popup_saved_rtv_;
    ComPtr<ID3D11ShaderResourceView> camp_popup_srv_;
    ComPtr<ID3D11DepthStencilView> camp_popup_dsv_,camp_popup_saved_dsv_;
    UINT camp_popup_width_=0,camp_popup_height_=0;
    bool camp_popup_active_=false,camp_popup_ready_=false;
    D3D12TextureRef camp_popup_d3d12_;   // renderer_backend=d3d12: the popup target (its own depth)
    D3D12TextureRef camp_art_d3d12_[3];  // renderer_backend=d3d12: frame, controls, construction art
    // Conversion buffers of the last flushed UI batch, reused by the next
    // cache-missing blits instead of reallocating and zero-filling them.
    std::vector<std::vector<std::uint32_t>> ui_blit_pixel_spares_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11VertexShader> native_vs_;
    ComPtr<ID3D11VertexShader> game_particle_vs_;
    ComPtr<ID3D11PixelShader> game_particle_ps_;
    ComPtr<ID3D11Buffer> game_particle_constants_, game_particle_buffer_;
    ComPtr<ID3D11ShaderResourceView> game_particle_srv_;
    UINT game_particle_capacity_ = 0;
    bool game_particle_pipeline_failed_ = false;
    HRESULT ensure_game_particle_pipeline();
    ComPtr<ID3D11VertexShader> game_ribbon_vs_, game_selection_vs_, game_mesh_vs_, game_glyph_vs_;
    // One draw of game effect elements (particles, ribbons) with the call's state.
    struct GameEffectDraw {
        const float* object_to_clip = nullptr;
        float clip_half_width = 0, clip_half_height = 0;
        UINT element_count = 0, vertex_count = 0, ribbon_points = 0;
        ID3D11VertexShader* vertex_shader = nullptr;
        WorldSupportShader vertex_program = WorldSupportShader::game_particle_vs;  // the same program (D3D12 backend)
        bool fog = true, reflection_depth = true;
        bool ribbon_antialias = false; // untextured UI ribbon side coverage
        const char* profile_pass = "";
    };
    std::vector<GameParticleGpu> game_effect_elements_;
    // The game effect constants of one draw (both backends).
    void game_effect_constants(const GameEffectDraw& draw, const FixedState& st, bool textured, GameParticleConstants& c) noexcept;
    HRESULT draw_game_effect_d3d12(const GameEffectDraw& draw, const FixedState& st, SurfaceImage* texture);
    HRESULT draw_game_effect(const GameEffectDraw& draw, const FixedState& state, SurfaceImage* texture);
    // Bloom glow target (world size): additive world particles add their light
    // here; valid for bloom only in the frame it was cleared (glow_frame_).
    ComPtr<ID3D11Texture2D> glow_;ComPtr<ID3D11RenderTargetView> glow_rtv_;ComPtr<ID3D11ShaderResourceView> glow_srv_;
    UINT glow_width_ = 0, glow_height_ = 0;std::uint64_t glow_frame_ = UINT64_MAX;
    std::vector<std::uint32_t> game_particle_order_;
    ComPtr<ID3D11PixelShader> native_ps_;
    ComPtr<ID3D11PixelShader> native_terrain_ps_; // NATIVE_TERRAIN_PS variant: terrain batches only
    // Terrain pixel variant plus the snow trail code (NATIVE_TERRAIN_PS and
    // NATIVE_SNOW_TRAILS), from the build-time shader table, bound to terrain
    // batches instead while a trail field exists.
    ComPtr<ID3D11PixelShader> native_snow_ps_;
    // The Low pixel path (NATIVE_LOW): the lean variant without the stages
    // native_low_pixel_path's settings switch off (Low terrain batches too).
    ComPtr<ID3D11PixelShader> native_low_ps_;
    // Low unlit ordered batches (NATIVE_LOW and NATIVE_UNLIT: the stock sky).
    ComPtr<ID3D11PixelShader> native_low_unlit_ps_;
    // The flat tile class (NATIVE_TERRAIN_FLAT): terrain and snow terrain
    // (NativeBatch::flat_pixels).
    ComPtr<ID3D11PixelShader> native_terrain_flat_ps_,native_snow_flat_ps_;
    bool terrain_variant_missing_reported_=false; // flush_native names missing terrain variants once
    // NATIVE_SHADOW_MASK copies (the material pass of prepassed batches reads
    // the sun-shadow mask): object, Low, then the terrain, snow terrain and
    // flat variants as above.
    ComPtr<ID3D11PixelShader> native_masked_ps_,native_masked_low_ps_,native_masked_terrain_ps_,native_masked_snow_ps_,
     native_masked_terrain_flat_ps_,native_masked_snow_flat_ps_;
    // Depth prepass and sun-shadow mask (flush_native; native_shader.h
    // nativePrepassPS / nativeShadowMaskPS): the prepass pixel entries (and
    // Low), the full-screen mask stages (and Low), the world-size targets
    // (receiver rest offset RGBA16F, terrain normal RG16, mask RG16F), a
    // depth-read-only view of the world depth and the pass states.
    ComPtr<ID3D11PixelShader> native_prepass_ps_,native_low_prepass_ps_,native_shadow_mask_ps_,native_low_shadow_mask_ps_;
    ComPtr<ID3D11VertexShader> native_shadow_mask_vs_;
    // The prepass vertex stages (position and the prepass pixel inputs only):
    // nativePrepassVS, and for tessellated terrain nativePrepassTessVS with the
    // plain and snow-trail hull and domain stages (ensure_terrain_tessellation).
    ComPtr<ID3D11VertexShader> native_prepass_vs_,terrain_prepass_tess_vs_;
    // Point-shadow atlas cell clear (nativeDepthClearVS, renderer_point_shadows.cpp).
    ComPtr<ID3D11VertexShader> native_depth_clear_vs_;ComPtr<ID3D11RasterizerState> native_point_clear_raster_;
    ComPtr<ID3D11HullShader> terrain_prepass_hs_,terrain_prepass_snow_hs_;
    ComPtr<ID3D11DomainShader> terrain_prepass_ds_,terrain_prepass_snow_ds_;
    ComPtr<ID3D11Texture2D> prepass_rest_,prepass_normal_,shadow_mask_;
    ComPtr<ID3D11RenderTargetView> prepass_rest_rtv_,prepass_normal_rtv_,shadow_mask_rtv_;
    ComPtr<ID3D11ShaderResourceView> prepass_rest_srv_,prepass_normal_srv_,shadow_mask_srv_;
    ComPtr<ID3D11DepthStencilView> world_depth_read_only_dsv_;
    std::array<ComPtr<ID3D11DepthStencilState>,2> prepass_depth_states_; // the game's LESS, LESSEQUAL; stencil 0x80 set
    ComPtr<ID3D11DepthStencilState> masked_depth_state_,shadow_mask_depth_state_;
    ComPtr<ID3D11RasterizerState> shadow_mask_raster_;
    std::vector<std::uint8_t> native_prepass_batches_; // per batch of the current flush: drawn in the prepass
    // Frame plan (frame_plan.h, renderer_frame.cpp): record a pass with the
    // ids of the given views, resolve an id to its D3D11 view, bind a pass's targets.
    const PlanPass& plan_pass(const char* name,ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,
                              std::uint32_t reads=0) noexcept;
    ID3D11RenderTargetView* plan_rtv(const PlanPass& pass) const noexcept;
    ID3D11DepthStencilView* plan_dsv(const PlanPass& pass) const noexcept;
    void bind_plan_targets(const PlanPass& pass) noexcept;
    HRESULT create_prepass_targets();
    HRESULT resolve_world_shadow_mask() noexcept; // the mask pass over the marked pixels (renderer.cpp)
    // Settings-specialised pixel programs (native_shader.h NATIVE_OFF_*,
    // NATIVE_FOG_VOLUME_QUALITY): the key names the features whose uploaded
    // values are 0 (bit 0 anti-tiling, 1 height blend, 2 contact, 3 cliffs,
    // 4 material blend, 5 point shadows, 6 sun shadows, 7 no atmosphere,
    // 8 atmosphere without the fog volume, 9 the cascade coverage debug view
    // (compiled only into that set), 10 soft point shadows off, 11 the fast
    // combiner only, bound per batch, 12 material classes off: no class
    // library or material_quality 0). Key 0 draws the general
    // programs (the default preset's set). Another key's set is created from
    // its prebuilt programs on worker threads once the key holds; the general
    // programs draw until it is ready (renderer.cpp). Every set built this
    // session stays resident, so a key seen before binds in the same frame.
    // All sets being built share one queue of programs over one pool.
    struct NativeSpecialisationJob;
    struct NativeSpecialisationPool;
    using NativeSpecialisedSet=std::array<ComPtr<ID3D11PixelShader>,15>;
    std::shared_ptr<NativeSpecialisationPool> native_spec_pool_;
    std::vector<std::shared_ptr<NativeSpecialisationJob>> native_spec_jobs_; // sets building, in start order
    bool native_specialisation_building(std::uint32_t key) const noexcept;
    std::uint32_t native_spec_candidate_=0;std::uint64_t native_spec_candidate_frame_=0;
    std::uint64_t native_spec_updated_frame_=~std::uint64_t{0}; // the last frame whose world flush chose a set
    std::unordered_map<std::uint32_t,NativeSpecialisedSet> native_spec_sets_;
    // The prebuilt specialised sets (shipped preset keys whose 15 programs
    // exist in the embedded table or beside the renderer), their cache files
    // and shader keys in NativeSpecialisedSet order.
    std::unordered_map<std::uint32_t,std::vector<std::wstring>> native_spec_files_;
    std::unordered_map<std::uint32_t,std::vector<ShaderKey>> native_spec_keys_;
    void find_prebuilt_native_specialisations() noexcept;
    const NativeSpecialisedSet* native_spec_active_=nullptr;
    std::uint32_t native_spec_terrain_bits_=0,native_spec_fog_bits_=0;bool native_sun_shadows_live_=true,native_point_soft_live_=true;
    std::uint32_t native_specialisation_key() const noexcept;
    void update_native_specialisation() noexcept;
    std::vector<std::uint32_t> native_spec_queue_; // startup sets still to build, in order
    void queue_startup_specialisations() noexcept;
    void pump_native_specialisation(bool start_queued) noexcept;
    // The current key and every sibling it prepares are resident, nothing
    // building (true when this frame's world flush chose no set).
    bool native_specialisation_settled() const noexcept;
    void release_native_specialisation() noexcept;
    ID3D11PixelShader* native_pixel(ComPtr<ID3D11PixelShader> Renderer::* member) const noexcept;
    static ComPtr<ID3D11PixelShader> Renderer::* const* native_specialised_members() noexcept; // in NativeSpecialisedSet order
    void start_native_specialisation(std::uint32_t key,bool first) noexcept; // first: front of the queue
    std::uint32_t native_spec_current_key_=0; // the key of the latest world flush
    ID3D11PixelShader* native_pixel(ComPtr<ID3D11PixelShader> Renderer::* member,bool contact,bool fast_combiner=false) const noexcept;
    std::set<std::uint64_t> native_general_combiners_; // general-combiner configurations reported once
    // Ground contact reach (contact_blend.h): the active map's contact ground
    // grid (the contact blend's t13) as per-cell maxima, and the configured
    // reach above it. A draw whose world bounds stay above every cell under
    // its footprint by more than the reach gets no contact on any pixel.
    std::vector<float> native_contact_cell_high_;std::uint32_t native_contact_cells_x_=0,native_contact_cells_y_=0;
    float native_contact_reach_=0;std::uint64_t native_contact_generation_=1;
    void build_native_contact_cells(const std::vector<float>& ground,std::uint32_t verts_x,std::uint32_t verts_y) noexcept;
    bool native_draw_may_contact(const NativeQueuedDraw& draw,UINT contact_map) const noexcept;
    bool fog_camera_valid_=false; // bind_world_fog: fogClipToWorld inverts this frame's world camera
    // Liquid surfaces (nativeLiquidPS): the liquid triangles of liquid batches,
    // and the same for the Low path.
    ComPtr<ID3D11PixelShader> native_liquid_ps_,native_low_liquid_ps_;
    // The water body without physical water, and the foam layer
    // (nativeLiquidPS variants; the Low body also serves batches drawn
    // without the reflection frame).
    ComPtr<ID3D11PixelShader> native_liquid_ssr_ps_,native_liquid_foam_ps_;
    // Half-resolution water reflection (flush_native trace_water): the liquid
    // input program (NATIVE_LIQUID_SSR_INPUT) and the march (nativeLiquidSsrPS).
    ComPtr<ID3D11PixelShader> native_liquid_ssr_input_ps_,native_liquid_ssr_march_ps_,native_liquid_ssr_filter_ps_;
    // Water-only batches (full, without physical water, Low/no reflection
    // frame) and lava-only batches (NATIVE_LIQUID_WATER / NATIVE_LIQUID_LAVA).
    ComPtr<ID3D11PixelShader> native_water_ps_,native_water_ssr_ps_,native_low_water_ps_,native_lava_ps_;
    unsigned native_liquid_types_reported_=0; // liquid_batch_types lines, by type mix
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11PixelShader> backdrop_ps_;
    ComPtr<ID3D11PixelShader> interface_scaled_ps_;bool interface_scaled_ps_failed_=false;
    bool interface_scaled_ps();
    ComPtr<ID3D11PixelShader> interface_text_ps_;bool interface_text_ps_failed_=false;
    bool interface_text_ps();
    ComPtr<ID3D11VertexShader> debug_texture_vs_;
    ComPtr<ID3D11PixelShader> debug_texture_ps_;
    ComPtr<ID3D11Buffer> debug_texture_constants_;
    ComPtr<ID3D11SamplerState> debug_texture_sampler_;
    ComPtr<ID3D11VertexShader> clear_vs_;
    ComPtr<ID3D11PixelShader> clear_ps_;
    ComPtr<ID3D11Buffer> clear_constants_;
    ComPtr<ID3D11RasterizerState> clear_raster_state_;
    ComPtr<ID3D11BlendState> clear_blend_state_;
    std::array<ComPtr<ID3D11DepthStencilState>, 3> clear_depth_states_;
    struct PendingScreenRectangle {
        ScreenRectangleRequest request{};
        D3DVIEWPORT7 viewport{};
    };
    std::vector<PendingScreenRectangle> pending_screen_rectangles_;
    ComPtr<ID3D11VertexShader> screen_rectangle_vs_;
    ComPtr<ID3D11PixelShader> screen_rectangle_ps_;
    ComPtr<ID3D11InputLayout> screen_rectangle_layout_;
    ComPtr<ID3D11Buffer> screen_rectangle_vertices_;
    UINT screen_rectangle_capacity_=0;
    ComPtr<ID3D11RasterizerState> screen_rectangle_raster_state_;
    ComPtr<ID3D11BlendState> screen_rectangle_blend_state_;
    ComPtr<ID3D11DepthStencilState> screen_rectangle_depth_state_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11Buffer> native_world_lighting_constants_;
    ComPtr<ID3D11Buffer> native_point_light_constants_, native_point_light_buffer_;
    ComPtr<ID3D11ShaderResourceView> native_point_light_srv_;
    // World X/Y light grid (point_light_grid.h), PS/CS t17.
    ComPtr<ID3D11Buffer> native_point_light_grid_buffer_;
    ComPtr<ID3D11ShaderResourceView> native_point_light_grid_srv_;
    ComPtr<ID3D11ComputeShader> native_build_cs_;
    ComPtr<ID3D11Buffer> native_lod_observations_;
    ComPtr<ID3D11UnorderedAccessView> native_lod_observation_uav_;
    D3D12BufferRef native_lod_observations12_;
    struct NativeLodReadback {
        std::uint64_t ticket12 = 0;
        ComPtr<ID3D11Buffer> buffer;
        std::shared_ptr<NativeLodSnapshot> snapshot;
    };
    std::array<NativeLodReadback,3> native_lod_readbacks_;
    std::shared_ptr<NativeLodSnapshot> native_lod_capture_;
    std::vector<UINT> native_lod_row_indices_;
    std::uint64_t native_lod_frame_ = UINT64_MAX;
    std::uint64_t native_lod_copied_request_ = 0, native_lod_published_request_ = 0;
    bool native_lod_failed_ = false;
    std::uint64_t native_lod_request_ = 0;
    std::uint64_t native_lod_wait_request_ = 0, native_lod_wait_frame_ = 0;
    const char* native_lod_wait_reason_ = "no_native_flush";
    double native_lod_wait_camera_max_delta_ = 0;
    bool native_lod_colour_pass() noexcept;
    void begin_native_lod_frame() noexcept;
    bool begin_native_lod_capture(const NativeSubmissionLayout& layout) noexcept;
    void finish_native_lod_frame() noexcept;
    void release_native_lod_observer() noexcept;
    ComPtr<ID3D11ComputeShader> native_pose_cs_;
    ComPtr<ID3D11ComputeShader> native_instance_update_cs_;
    ComPtr<ID3D11Buffer> native_persistent_instances_, native_instance_updates_, native_world_clip_constants_;
    ComPtr<ID3D11ShaderResourceView> native_persistent_instance_srv_, native_instance_update_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_persistent_instance_uav_;
    UINT native_persistent_instance_capacity_ = 0, native_instance_update_capacity_ = 0;
    // Per-flush structured inputs (instance updates, pose inputs) appended
    // into fenced per-frame rings (FencedUploadRing, constant_ring.h): each
    // upload is written with NO_OVERWRITE and read from its first element
    // (b4 work range), and no buffer the GPU may still read is discarded.
    // Each 3.7 MB DISCARD per flush (several a frame while records changed)
    // ran the driver out of renamed copies: 2-9 ms Map stalls (zone 3 route).
    // Needs D3D11.1 MapNoOverwriteOnDynamicBufferSRV; without it each upload
    // keeps its own buffer with one DISCARD. Same owner thread as the
    // constant ring; its frame boundary is restart_upload_rings.
    owned::FencedUploadRing native_instance_update_ring_{sizeof(NativeInstanceUpdate)};
    bool structured_ring_supported_ = false;
    std::vector<NativeInstanceRecord> native_instance_records_;
    std::unordered_map<std::uint64_t,NativeStaticShadowRecord> native_static_shadow_records_;
    std::uint64_t native_static_shadow_visit_ = 0;
    // Bumped by every insert into or erase from native_static_shadow_records_
    // outside the bake rebuild: the bake's record membership key.
    std::uint64_t native_static_shadow_records_serial_ = 1;
    // Persistent static sun casters drawn from one baked layout (issue #162).
    // The bridge sends its static caster store once per store revision; the
    // bake holds every store caster that has a current depth record, with
    // record and range references (the static colour set rule, L-046), and
    // its own work buffer (layout slot 5). A static-layer redraw draws the
    // bake with no per-caster CPU work: the GPU culls each record per map
    // (nativeLightBoundsVisible) and, for a scrolled map, against the
    // exposed strip. Store casters without a current record wait in `absent`
    // and are registered through the depth path when a redrawn map reaches
    // them, which changes the record serial and rebuilds the bake.
    struct NativeStaticShadowBake {
        std::vector<RetainedShadowCasterValue> store;
        std::uint64_t store_revision = 0;
        std::vector<NativeSelectedRecord> records;
        std::vector<RetainedShadowCasterValue> absent;
        std::vector<std::weak_ptr<const void>> owners;   // distinct record owners
        std::vector<UINT> mesh_slots, material_slots;     // distinct, ascending
        std::vector<UINT> dispatch_chunks, geometry_chunks; // per dispatch range of the layout
        // Store key (id << 32 | part) -> work index and records index, by key:
        // a changed or removed caster's work entry is switched off in place.
        // shared: its work entry prepares a world range other baked entries
        // also draw from, so it cannot be switched off alone.
        struct Entry { std::uint64_t key = 0; NativeShadowCasterRevision revision{}; UINT work = UINT_MAX, record = UINT_MAX; bool shared = false; };
        // Owner of each record (index into owners) and the live records per
        // owner: a switched-off record's owner may expire without a rebuild.
        std::vector<UINT> owner_of, owner_live;
        std::vector<Entry> entries;
        UINT excluded = 0;
        std::uint64_t built_store_revision = 0, built_content_revision = 0, built_records_serial = 0;
        std::uint64_t prepared_vertices = 0;
        UINT preparations = 0;
        bool built = false, far_foliage_opaque = false;
        unsigned layout_slot = 5;
        // Terrain bake: per-work map masks of this redraw (t35), set by the
        // bridge's final terrain masks instead of the GPU strip test.
        bool masked = false;
        std::vector<UINT> masks;
        // Sun maps its records reach (bits 0-4; the neighbour bake follows
        // shadow_neighbour_casters) and, for the neighbour bake, the caster
        // key it was built at (neighbour_caster_key).
        UINT slice_mask = 0x1fu;
        std::uint64_t built_key = 0;
    } native_static_shadow_bake_, native_terrain_shadow_bake_;
    // Neighbour-map decorations as sun and shelter casters (layout slot 7):
    // their object-space colour records cast as themselves (no depth records,
    // no world allocations), drawn into the static layer with the store bake
    // and pancaked onto the near plane (their depth is not in the bridge's
    // cascade depth grid). Membership changes bump neighbour_caster_revision_.
    NativeStaticShadowBake native_neighbour_shadow_bake_{};
    std::uint64_t neighbour_caster_revision_ = 1;
    // The neighbour caster key each sun map's static layer was drawn with.
    std::array<std::uint64_t,5> native_shadow_neighbour_drawn_{};
    // Revision and shadow_neighbour_casters level (0 off, 1 close maps, 2 all).
    std::uint64_t neighbour_caster_key() const noexcept;
    UINT neighbour_caster_slices() const noexcept;
    HRESULT draw_native_neighbour_shadows() noexcept;
    HRESULT finish_native_shadow_bake(NativeStaticShadowBake& bake) noexcept;
    // Terrain sun caster records registered through the depth path (same
    // currency rule as native_static_shadow_records_), for the terrain bake.
    std::unordered_map<std::uint64_t,NativeStaticShadowRecord> native_terrain_shadow_records_;
    std::uint64_t native_terrain_shadow_records_serial_ = 1;
    ComPtr<ID3D11Buffer> native_shadow_work_mask_;
    ComPtr<ID3D11ShaderResourceView> native_shadow_work_mask_srv_;
    UINT native_shadow_work_mask_capacity_ = 0;
    D3D12BufferRef native_shadow_work_mask12_;  // D3D12 backend: the same masks (t35)
    NativeStaticShadowBake* native_shadow_bake_active_ = nullptr;   // flush_native_shadows draws this bake
    bool native_shadow_bake_current(const NativeStaticShadowBake& bake,std::uint64_t records_serial) const noexcept;
    HRESULT rebuild_native_shadow_bake(NativeStaticShadowBake& bake,
        std::unordered_map<std::uint64_t,NativeStaticShadowRecord>& stored,
        const std::vector<std::pair<std::uint64_t,NativeShadowCasterRevision>>& members,
        std::vector<std::size_t>& absent,bool sweep,std::uint64_t records_serial) noexcept;
    void release_native_shadow_bake(NativeStaticShadowBake& bake) noexcept;
    HRESULT draw_native_shadow_bake(NativeStaticShadowBake& bake) noexcept;
    bool native_static_shadow_record_current(const NativeStaticShadowRecord& stored,
        const NativeShadowCasterRevision& revision) const noexcept;
    bool update_native_static_shadow_store(const RetainedShadowCasterValue* casters,std::uint32_t count) noexcept;
#ifdef EID3D11_TESTING
    std::uint64_t test_native_depth_selections_ = 0, test_native_depth_registrations_ = 0;
#endif
    std::vector<UINT> native_instance_free_, native_instance_dirty_;
    std::vector<NativeInstanceUpdate> native_instance_updates_staging_;
    std::array<float,16> native_world_clip_{};
    // Debug freeze: persistent-instance GPU culling (and the bridge's CPU
    // visibility) use this camera while projection keeps the live one.
    std::array<float,16> native_cull_clip_{};
    bool native_cull_frozen_ = false;
    // Screen-size culling: draws below this fraction of the screen height are
    // skipped; sun-cascade casters below the texel size likewise.
    float native_cull_min_screen_ = .005f, native_shadow_cull_min_texels_ = 2.f;
    // draw_distance (metres), the camera-pass distance cull last uploaded,
    // and where the world frame's fog hides everything (0: nowhere) and when.
    float native_draw_distance_ = 300.f, native_distance_cull_uploaded_ = -1.f;
    float native_lod_error_px_ = 1.f, native_lod_error_uploaded_ = -1.f; // static LOD pixel limit (0: off)
    float native_impostor_pixels_ = 64.f, native_impostor_pixels_uploaded_ = -1.f; // crown impostor size (0: off)
    bool native_impostor_debug_ = false, native_impostor_debug_uploaded_ = false; // F10 impostor_debug (magenta quads)
    // Crown impostors (renderer_impostors.cpp, static_lod.h): the atlas array
    // (albedo with coverage, object-space normals; a slice per cut-out figure
    // and texture), its bake pipeline and the slices of the current
    // neighbours' figures.
    struct ImpostorAtlas {
        ComPtr<ID3D11Texture2D> albedo, normal, depth;
        ComPtr<ID3D11ShaderResourceView> albedo_srv, normal_srv;
        ComPtr<ID3D11DepthStencilView> depth_view;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11Buffer> constants;
        ComPtr<ID3D11RasterizerState> raster;
        ComPtr<ID3D11DepthStencilState> depth_state;
        ComPtr<ID3D11BlendState> blend;
        ComPtr<ID3D11SamplerState> sampler;
        // renderer_backend=d3d12: the atlas arrays (storage textures, so each
        // slice takes generate_mips) and the bake's colour pair with its D32
        // depth, one slice's 4x4 frames; each bake is copied into its slice.
        D3D12TextureRef albedo12, normal12, bake_albedo12, bake_normal12;
        UINT capacity = 0, count = 0;
        std::unordered_map<std::string, UINT> slices;
        bool failed = false;   // the reason was logged once; crowns draw their triangles
        UINT baked = 0;
        double bake_ms = 0;
        // renderer_backend=d3d12, temporary (the D3D12 bake cost question): its
        // texture lookups, buffer finishes, atlas creation, draws + copies, mips.
    } impostors_;
    HRESULT bake_neighbour_impostors(const NeighbourPiece& upload) noexcept;
    HRESULT bake_neighbour_impostors_d3d12(const NeighbourPiece& upload) noexcept;
    HRESULT ensure_impostor_atlas(UINT slices) noexcept;
    HRESULT ensure_impostor_atlas_d3d12(UINT slices) noexcept;
    void impostor_failure(const char* reason) noexcept;
    std::uint32_t neighbour_impostor_slice(const ResourceIdentity& figure, std::uint64_t texture) const;
    float fog_cull_distance_ = 0.f;
    std::uint64_t fog_cull_frame_ = 0;
    // Occlusion culling (renderer_occlusion.cpp): a max-depth pyramid built
    // after opaque terrain; draws hidden behind it are skipped.
    HRESULT build_native_hiz() noexcept;
    bool native_occlusion_active() const noexcept;
    bool native_occlusion_enabled_ = true, native_hiz_failed_ = false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> native_hiz_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> native_hiz_srv_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> native_hiz_uav_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> native_hiz_mip_srv_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> native_hiz_copy_cs_, native_hiz_reduce_cs_;
    UINT native_hiz_width_ = 0, native_hiz_height_ = 0, native_hiz_mips_ = 0;
    std::uint64_t native_hiz_frame_ = UINT64_MAX;
    std::array<float,16> native_hiz_clip_{};
    bool native_world_clip_valid_ = false, native_world_clip_dirty_ = false;
    ComPtr<ID3D11Buffer> native_animation_, native_pose_inputs_, native_pose_nodes_;
    ComPtr<ID3D11ShaderResourceView> native_animation_srv_, native_pose_input_srv_, native_pose_node_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_pose_node_uav_;
    UINT native_animation_capacity_ = 0, native_animation_used_ = 16;
    ComPtr<ID3D11UnorderedAccessView> native_animation_uav_;
    ComPtr<ID3D11ComputeShader> native_terrain_phase_cs_;
    // One slot per terrain phase identity (the fields same_prepared_identity
    // compares without the part). The lookup lives here, never in the game's
    // phase values: the scene thread resolves a per-command copy of them
    // (landmine: game-side cache handles under scene-thread command copies).
    struct NativeTerrainPhaseKey {
        std::int32_t signed_id=0;
        std::uint32_t set_index=0;
        std::uint64_t lifecycle_generation=0,world_epoch=0,plan_generation=0;
        bool terrain_source=false;
        bool operator==(const NativeTerrainPhaseKey& other) const noexcept {
            return signed_id==other.signed_id&&set_index==other.set_index&&
                lifecycle_generation==other.lifecycle_generation&&world_epoch==other.world_epoch&&
                plan_generation==other.plan_generation&&terrain_source==other.terrain_source;
        }
    };
    struct NativeTerrainPhaseKeyHash {
        std::size_t operator()(const NativeTerrainPhaseKey& key) const noexcept {
            std::uint64_t value=std::uint64_t(std::uint32_t(key.signed_id))|(std::uint64_t(key.set_index)<<32)|
                (std::uint64_t(key.terrain_source)<<63);
            for(const std::uint64_t part:{key.lifecycle_generation,key.world_epoch,key.plan_generation})
                value=(value^part)*0x100000001b3ull;
            return static_cast<std::size_t>(value^(value>>32));
        }
    };
    struct NativeTerrainPhaseSlot {
        NativeTerrainPhaseKey key{};
        std::weak_ptr<const void> owner;
        std::uint64_t revision=0,last_frame=UINT64_MAX;
        UINT allocation=UINT_MAX,count=0;
        bool active=false,valid=false,scheduled=false;
    };
    struct NativeTerrainPhaseWork {
        UINT slot=UINT_MAX,allocation=UINT_MAX,offset=0,count=0;
        std::uint64_t revision=0;
    };
    std::vector<NativeTerrainPhaseSlot> native_terrain_phases_;
    std::unordered_map<NativeTerrainPhaseKey,UINT,NativeTerrainPhaseKeyHash> native_terrain_phase_lookup_;
    std::vector<UINT> native_terrain_phase_free_;
    // Slots resolved in native_terrain_phase_live_frame_: the live sector
    // count for the once-only growth guard.
    std::uint64_t native_terrain_phase_live_frame_=UINT64_MAX;
    UINT native_terrain_phase_live_=0;
    bool native_terrain_phase_growth_logged_=false;
    std::vector<NativeTerrainPhaseWork> native_terrain_phase_work_;
    std::vector<NativeDrawWorkGpu> native_terrain_phase_copies_;
    UINT native_pose_input_capacity_ = 0, native_pose_node_capacity_ = 0, native_pose_node_used_ = 0;
    owned::FencedUploadRing native_pose_input_ring_{sizeof(NativePoseGpu)}; // see native_instance_update_ring_
    std::unordered_map<std::string,NativeSkeletonAsset> native_skeleton_assets_;
    std::vector<NativePoseSlot> native_pose_slots_;
    std::vector<NativeGpuRange> native_pose_ranges_, native_world_ranges_, native_animation_ranges_;
    // Free ranges per NativeRangeBuffer ordered by (count, index), so the first
    // entry at or above a request is exactly the linear scan's best fit. Entries
    // are validated (and stale ones dropped) when allocate_native_range visits
    // them. A failed insert leaves that buffer on the linear scan until reset.
    std::array<std::set<std::pair<UINT,UINT>>,3> native_free_ranges_;
    std::array<bool,3> native_free_ranges_complete_{true,true,true};
    std::vector<NativePoseWork> native_pose_work_;
    std::vector<NativePoseGpu> native_pose_upload_;
#ifdef EID3D11_TESTING
    std::uint64_t test_native_pose_nodes_evaluated_ = 0, test_native_instance_rows_uploaded_ = 0;
    std::uint64_t test_native_command_values_written_ = 0;
    std::uint64_t test_native_descriptors_uploaded_ = 0, test_native_work_upload_bytes_ = 0;
    std::uint64_t test_native_material_groups_created_ = 0;
    std::uint64_t test_native_material_group_trim_scans_ = 0;
    std::uint64_t test_native_material_reconstructions_ = 0;
    std::uint64_t test_native_prepared_material_hits_ = 0;
    std::uint64_t test_native_layout_rebuilds_ = 0;
    std::uint64_t test_native_terrain_phase_upload_bytes_ = 0;
#endif
    int cursor_scale_percent_ = 100;
    // Gamma ramp (set_gamma_ramp), applied to the finished frame before
    // Present (apply_gamma_ramp); an identity ramp costs nothing.
    HRESULT apply_gamma_ramp() noexcept;
    mutable std::mutex gamma_lock_;
    std::array<WORD,768> gamma_ramp_{};
    bool gamma_identity_ = true, gamma_changed_ = false, gamma_set_ = false, gamma_failed_ = false;
    ComPtr<ID3D11Texture2D> gamma_lut_, gamma_copy_;
    ComPtr<ID3D11ShaderResourceView> gamma_lut_srv_, gamma_copy_srv_;
    ComPtr<ID3D11VertexShader> gamma_vs_;
    ComPtr<ID3D11PixelShader> gamma_ps_;
    ComPtr<ID3D11SamplerState> gamma_sampler_;
    // D3D12 backend: the same pass (apply_gamma_ramp_d3d12): the LUT, the
    // back buffer copy it reads, and gammaVS/gammaPS.
    // source/target (FSR frame generation's HUD-less image): the same ramp
    // from source into target instead of over the back buffer.
    HRESULT apply_gamma_ramp_d3d12(D3D12Texture* source=nullptr,D3D12Texture* target=nullptr) noexcept;
    D3D12TextureRef gamma_lut_d3d12_, gamma_frame_d3d12_;
    // Frame generation's inputs (fg_frame_image, D3D12): the HUD-less world
    // (upscaled and post-processed, before any interface) copied each world
    // frame; the frame image itself is the executor's (set_frame_image).
    bool fg_frame_image_ = false;
    D3D12TextureRef hudless_d3d12_;
    bool world_output_this_present_ = false, hudless_this_present_ = false;
    // Why this present is not a game frame (frame generation must not
    // interpolate it): kFgNot* bits, 0 for a game frame; logged on change.
    // kFgNotFullInterface: the interface quads drawn this present cover at
    // least kFullInterfaceCoverage of the output (trade, outfit, camp and
    // other full-screen panels over a live world): a 64x36 cell grid over
    // our own UI quad rectangles (ui_quad_cells_; a cell counts when one quad
    // covers it whole; colour-keyed panels count, text glyphs do not).
    // Measured 2026-11-01 (bf4f705f/86362505 runs, 4K): world HUD 0-12%,
    // trade 51%, outfit 44%; an "opaque only" cover (not keyed) missed the
    // trade panels (7%) and the outfit (0%), so every quad counts.
    enum : unsigned { kFgNotWorld = 1, kFgNotLoadingPicture = 2, kFgNotRevealHold = 4, kFgNotInFront = 8, kFgNotFullInterface = 16 };
    static constexpr unsigned kUiCoverageColumns = 64, kUiCoverageRows = 36;
    static constexpr float kFullInterfaceCoverage = 0.3f;  // between the HUD's 12% and the panels' 44-51%
    std::array<std::uint64_t, kUiCoverageRows> ui_quad_cells_{};
    int ui_coverage_logged_ = -1;  // the last logged cover (5% steps)
    void mark_interface_cells(float x0, float y0, float x1, float y1) noexcept;
    float interface_coverage() const noexcept;
    unsigned fg_not_game_frame_ = 0, fg_not_game_frame_logged_ = ~0u;
    // Why a game frame was not generated from (frame generation inputs
    // missing this present; null: generated); logged on change.
    const char* fg_skip_logged_ = "";
    void note_frame_generation(const char* skipped) noexcept;
    void capture_hudless_d3d12() noexcept;
    // frame_generation (D3D12; 1 DLSS-G, 2 FSR): this present's mode, the
    // upscaler frame whose depth and motion it takes (begin_upscaler_frame),
    // whether the previous present was generated from (else the generator
    // resets), the reasons a mode cannot run (DLSS-G's host capability; a
    // failure that turned a mode off for the session), and the measured
    // present rates.
    int frame_generation_mode_ = 0;
    bool fg_caps_checked_ = false, fg_previous_generated_ = false;
    char fg_dlssg_unavailable_[160] = {}, fg_failed_[3][200] = {};
    int fg_reason_mode_ = -1;  // the mode whose reason the F10 note shows
    // FSR: this present was prepared (FidelityFX gets the HUD-less image),
    // the time of the previous prepare, and the gamma-mapped HUD-less copy
    // (the back buffer it is compared with carries the gamma ramp).
    bool fsr_prepared_ = false;
    std::int64_t fsr_prepare_tick_ = 0;
    D3D12TextureRef hudless_gamma_d3d12_;
    bool prepare_fsr_frame_generation_d3d12() noexcept;
    D3D12Texture* fsr_hudless_d3d12() noexcept;
    bool fg_upscaler_valid_ = false, fg_upscaler_reset_ = true;
    float fg_clip_to_world_[16]{};
    std::array<float,16> fg_world_to_clip_{}, fg_previous_clip_{};
    float fg_jitter_[2]{};
    std::uint64_t fg_stats_real_ = 0, fg_stats_generated_ = 0;
    std::int64_t fg_stats_tick_ = 0;
    double fg_presented_fps_ = 0.0, fg_real_fps_ = 0.0;
    // Auto-off at a low rendered rate (frame_generation_min_fps; FidelityFX
    // frame interpolation swap chain guide, "Minimum Framerate"): game-frame
    // present intervals averaged per 1 s window. Off after 2 windows below
    // the minimum; on again after 3 windows whose rate, less the measured
    // cost of generating (the frame time on before the switch minus the
    // first window off), still clears the minimum by 10%.
    bool fg_low_rate_ = false, fg_rate_previous_game_ = false;
    int fg_low_rate_windows_ = 0;
    double fg_rate_window_ms_ = 0.0, fg_rate_on_ms_ = 0.0, fg_rate_cost_ms_ = -1.0;
    unsigned fg_rate_window_count_ = 0;
    void update_frame_generation_auto_off(int output_limit) noexcept;
    bool evaluate_frame_generation_d3d12() noexcept;
    void frame_generation_off(int mode,const char* reason) noexcept;
    void update_frame_generation_rates() noexcept;
    std::vector<ShaderProgram> gamma_programs_d3d12_;
    ComPtr<ID3D11ComputeShader> native_shadow_prepare_cs_, native_shadow_commit_cs_, native_shadow_build_cs_;
    UINT native_shadow_chunks(const NativeSubmissionLayout& layout,const NativeDispatchRange& range,bool triangles) const noexcept;
    ComPtr<ID3D11VertexShader> native_shadow_vs_;
    ComPtr<ID3D11PixelShader> native_shadow_ps_;
    ComPtr<ID3D11Texture2D> native_shadow_depth_;
    ComPtr<ID3D11ShaderResourceView> native_shadow_depth_srv_;
    std::array<ComPtr<ID3D11DepthStencilView>,5> native_shadow_dsv_;
    // Early-out cells of the sun maps (t64; build_native_shadow_range): per
    // 16-texel cell the nearest depth and one minus the farthest, dilated.
    // native_shadow_range_mask_: slices whose cells match their map.
    ComPtr<ID3D11Texture2D> native_shadow_cells_, native_shadow_range_;
    ComPtr<ID3D11ShaderResourceView> native_shadow_cells_srv_, native_shadow_range_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_shadow_cells_uav_, native_shadow_range_uav_;
    ComPtr<ID3D11ComputeShader> native_shadow_reduce_cs_, native_shadow_dilate_cs_;
    ComPtr<ID3D11Buffer> native_shadow_range_constants_;
    UINT native_shadow_range_side_ = 0;
    std::uint8_t native_shadow_range_mask_ = 0;
    bool native_shadow_early_out_ = true, native_shadow_range_failed_ = false;
    std::array<ComPtr<ID3D11Texture2D>,2> shadow_history_mask_;
    std::array<ComPtr<ID3D11RenderTargetView>,2> shadow_history_rtv_;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> shadow_history_srv_;
    ComPtr<ID3D11Texture2D> shadow_history_depth_;
    ComPtr<ID3D11ShaderResourceView> shadow_history_depth_srv_;
    ComPtr<ID3D11Buffer> shadow_history_constants_;
    ComPtr<ID3D11SamplerState> shadow_history_sampler_;
    std::array<float,16> shadow_history_previous_clip_{};
    std::uint64_t shadow_history_previous_epoch_=0;
    std::uint64_t shadow_history_previous_frame_=UINT64_MAX;
    std::uint64_t shadow_history_current_frame_=UINT64_MAX;
    std::uint64_t shadow_history_current_epoch_=0;
    unsigned shadow_history_previous_index_=0,shadow_history_current_index_=1;
    bool shadow_history_valid_=false,shadow_history_written_=false;
    bool shadow_history_committed_=false;
    struct NativePointShadowPage {
        ComPtr<ID3D11Texture2D> depth;
        D3D12TextureRef depth12;
        ComPtr<ID3D11ShaderResourceView> srv;
        std::vector<ComPtr<ID3D11DepthStencilView>> dsv;
        std::vector<bool> used;
        bool overlay=false;
    };
    std::vector<NativePointShadowPage> native_point_shadow_pages_;
    void report_shadow_diagnostics(const char* reason) noexcept;
    std::vector<RetainedWorldPointLight> native_point_light_inputs_;
    NativePointShadowStats point_shadows_cumulative_{};
    std::uint64_t point_shadow_epoch_=0,point_pass_skipped_frames_=0,point_publication_missing_frames_=0;
    bool point_pass_seen_=false,point_publication_seen_=false;
    ShadowRendererDiagnostics shadow_renderer_cumulative_{};
    std::array<bool,static_cast<unsigned>(ShadowRendererDifference::count)> shadow_renderer_seen_{};
    bool native_point_shadow_failure_reported_=false;
    UINT native_point_shadow_slot_=0;
    bool native_point_shadow_active_=false;
    std::uint64_t native_point_shadow_generation_=0;
    NativePointShadowFrame native_point_shadow_frame_{};
    ComPtr<ID3D11Buffer> native_point_shadow_constants_;
    ComPtr<ID3D11Buffer> native_shadow_constants_;
    ComPtr<ID3D11SamplerState> native_shadow_sampler_;
    ComPtr<ID3D11SamplerState> native_shadow_far_sampler_;
    ComPtr<ID3D11SamplerState> native_shadow_gather_sampler_; // PS s10: shadowPlaneCompare depth reads
    ComPtr<ID3D11SamplerState> native_point_shadow_sampler_;
    NativeShadowFrame native_shadow_frame_{};
    std::array<bool,5> native_shadow_valid_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> native_shadow_static_depth_;
    std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>,5> native_shadow_static_dsv_;
    std::array<bool,5> native_shadow_static_valid_{};
    // Camera each slice's current content was selected for (far deferral).
    std::array<std::array<float,16>,5> native_shadow_drawn_camera_{};
    // Consecutive frames each far map (C3, C4) has waited for its static
    // redraw behind a higher-priority map (L-081).
    std::array<std::uint8_t,2> native_shadow_far_waits_{};
    std::uint8_t native_shadow_full_dirty_mask_ = 0, native_shadow_static_dirty_mask_ = 0;
    std::uint8_t native_shadow_static_cleared_mask_ = 0;
    std::uint8_t native_shadow_retained_mask_ = 0; // far slices drawn in a retained projection
    // Close slices whose cached static layer scrolls by whole texels this
    // frame (shadow_far_retention.h close_shadow_scroll) and the shift.
    std::uint8_t native_shadow_scroll_mask_ = 0;
    std::array<CloseShadowScroll,5> native_shadow_scroll_{};
    ComPtr<ID3D11VertexShader> shadow_scroll_vs_;
    ComPtr<ID3D11PixelShader> shadow_scroll_ps_;
    ComPtr<ID3D11Buffer> shadow_scroll_constants_;
    ComPtr<ID3D11RasterizerState> shadow_scroll_raster_;
    // Terrain parts that entered each scrolled map's list (drawn whole).
    std::array<std::vector<std::uint64_t>,5> native_shadow_entered_{};
    UINT native_static_scroll_mask(const NativeWorldBounds& bounds,UINT mask,
        const NativeShadowCasterRevision* terrain=nullptr) const noexcept;
    UINT native_static_shadow_reach(const NativeWorldBounds& bounds,UINT mask) const noexcept;
    HRESULT scroll_native_static_shadow(UINT slice) noexcept;
    unsigned native_shadow_phase_ = 0;   // 0 single pass, 1 static layer, 2 dynamic overlay
    std::uint8_t native_shadow_dirty_mask_ = 0;
    std::uint8_t native_shadow_cleared_mask_ = 0;
    // Close maps whose ground cover and critter casters were drawn in this
    // collection (a flush can run more than once).
    std::uint8_t native_shadow_extras_mask_ = 0;
    bool native_shadow_collecting_ = false, native_shadow_failed_ = false;
    // Atomic: the game thread's frame-state sink reads it in scene thread mode.
    std::atomic<bool> native_shadows_enabled_{true};
    bool native_shadow_constants_dirty_ = false;
    UINT native_shadow_side_ = 2048, native_point_shadow_side_ = 256;
    std::array<float,4> native_shadow_softness_{}; // PCSS constants (shadowBias[5]); zero keeps the tapered PCF
    std::uint8_t native_close_shadow_cascades_ = 3;
    bool native_far_shadows_enabled_ = true, native_point_shadows_enabled_ = true;
    bool native_far_foliage_opaque_ = true;
    std::uint64_t native_shadow_draws_ = 0, native_shadow_triangles_ = 0;
    std::uint64_t native_shadow_prepared_records_ = 0, native_shadow_reused_slices_ = 0;
    std::uint64_t native_shadow_value_reuses_ = 0;
    std::uint64_t native_shadow_validity_scans_ = 0, native_shadow_frame_copies_ = 0;
    std::vector<NativeGeometryPage> native_geometry_pages_;
    ComPtr<ID3D11Buffer> native_draws_;
    ComPtr<ID3D11ShaderResourceView> native_draw_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_draw_uav_;
    ComPtr<ID3D11Buffer> native_draw_work_;
    ComPtr<ID3D11ShaderResourceView> native_draw_work_srv_;
    ComPtr<ID3D11Buffer> native_visible_triangles_;
    ComPtr<ID3D11ShaderResourceView> native_visible_triangle_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_visible_triangle_uav_;
    ComPtr<ID3D11Buffer> native_prepared_vertices_;
    ComPtr<ID3D11ShaderResourceView> native_prepared_vertex_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_prepared_vertex_uav_;
    ComPtr<ID3D11Buffer> native_world_vertices_;
    ComPtr<ID3D11ShaderResourceView> native_world_vertex_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_world_vertex_uav_;
    ComPtr<ID3D11Buffer> native_world_states_;
    ComPtr<ID3D11ShaderResourceView> native_world_state_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_world_state_uav_;
    UINT native_world_state_capacity_ = 0;
    ComPtr<ID3D11Buffer> native_indirect_args_;
    ComPtr<ID3D11UnorderedAccessView> native_indirect_args_uav_;
    ComPtr<ID3D11Buffer> native_work_range_constants_;
    ComPtr<ID3D11Buffer> dynamic_vertices_;
    ComPtr<ID3D11Buffer> dynamic_indices_;
    ComPtr<ID3D11Buffer> native_dynamic_;
    ComPtr<ID3D11ShaderResourceView> native_dynamic_srv_;
    std::vector<std::uint8_t> native_pending_upload_;
    struct NativeTerrainPhaseAllocation { UINT offset = 0; UINT count = 0; };
    std::unordered_map<std::uintptr_t, NativeTerrainPhaseAllocation>
        native_terrain_phase_allocations_;
    // The existing record owner pins complete commands; the ordering queue
    // contains stable record indices and pass masks, including pending prior generations.
    std::vector<NativeSelectedRecord> native_pending_draws_;
    std::vector<NativeSelectedRecord> native_registration_;
    std::vector<NativeStateBucket> native_state_buckets_;
    std::unordered_map<NativeMaterialKey, UINT, NativeMaterialKeyHash> native_state_lookup_;
    bool native_material_groups_dirty_ = false;
    std::vector<UINT> native_active_groups_;
    std::vector<std::size_t> native_draw_order_;
    // 0 per-frame color, 1 shadows, 2 persistent static color set, 3 static
    // terrain set, 4 neighbour opaque set, 5 static sun caster bake, 6 terrain
    // sun caster bake, 7 neighbour sun caster bake.
    // 8..22 map opaque/cutout; 23..37 map fixed ordered layers. Each of
    // the at most15 native sections owns two distinct retained layouts.
    static constexpr unsigned kMapPreviewSections = 15;
    static constexpr unsigned kMapOpaqueLayout = 8;
    static constexpr unsigned kMapOrderedLayout = kMapOpaqueLayout + kMapPreviewSections;
    std::array<NativeSubmissionLayout,kMapOrderedLayout + kMapPreviewSections> native_submission_layouts_;
    // Each registered key's draws hold one reference (taken over from its
    // registration enqueue) until removed or its owner expires.
    struct NativeStaticSet {
        std::unordered_map<std::uint64_t,std::vector<NativeSelectedRecord>> objects;
        std::vector<NativeSelectedRecord> draws;
        bool dirty = false;
        std::uint64_t revision = 1;
        unsigned layout_slot = 2;
        // The draws' distinct record owners (adjacent duplicates folded),
        // rebuilt with draws: owner expiry is the one membership change no
        // set or remove call announces.
        std::vector<std::weak_ptr<const void>> owners;
        // Object colour set only: keys registered since the last rebuild,
        // not in the bake (adopt_native_static_draws), and their draw count.
        std::vector<std::uint64_t> added;
        std::size_t added_draws = 0;
    };
    // Added draws a static set carries before a rebuild folds them in (at
    // least; an eighth of the baked draws when larger).
    static constexpr unsigned kNativeStaticAddedFold = 256;
    NativeStaticSet native_static_set_{}, native_terrain_set_{{},{},false,1,3};
    std::array<NativeStaticSet,kMapPreviewSections> map_preview_opaque_, map_preview_ordered_;
    std::array<std::uint64_t,kMapPreviewSections> map_preview_resource_revision_{};
    std::array<bool,kMapPreviewSections> map_preview_discovered_{};
    std::uint64_t map_preview_native_generation_ = 0;
    struct NeighbourGpuState {
        std::shared_ptr<const void> owner;
        std::weak_ptr<const void> weak_owner;
        NativeStaticSet opaque{{},{},false,1,4};
        std::vector<NativeSelectedRecord> ordered;
        struct AstralRecord { UINT record; NativePreparedPartValues base; };
        std::vector<AstralRecord> astral;
        std::uint64_t epoch = 0, renderer_generation = 0;
        std::uint64_t geometry_bytes = 0, texture_bytes = 0, peak_decoded_bytes = 0;
        std::uint32_t next_part = 0;
        std::string error;
        // Accepted neighbour rectangles: the active map's border fog moves
        // out only over these.
        std::vector<NeighbourBounds> accepted;
        double fog_range = 1; // range the uploaded content was fog-culled at
    };
    NeighbourGpuState neighbours_;
    // Sun-shadow receiver cells (32 m, world space) of the accepted neighbours'
    // terrain and decorations, published to the bridge (neighbour_receivers_publish).
    std::map<std::pair<int,int>,NativeWorldBounds> neighbour_receiver_cells_;
    // World-space neighbour seam fog of the current world frame (all sides
    // uncovered when no neighbour is rendered); set with ambient_terrain_state_.
    NeighbourFogField neighbour_fog_{};
    bool neighbour_pieces_overflow_logged_=false;
public:
    // Neighbour ground cover places nothing where
    // neighbour_fog_hides(world_box, neighbour_fog_world()) holds.
    const NeighbourFogField& neighbour_fog_world() const noexcept {return neighbour_fog_;}
private:
    void release_neighbour_maps() noexcept;
    // Prebuilt neighbour geometry: adopt the upload's pages, and afterwards
    // release every prebuilt mesh no draw registered (fog culled, failures).
    void adopt_prebuilt_pages(const NeighbourPiece& upload) noexcept;
    void adopt_prebuilt_textures(const NeighbourPiece& upload) noexcept;
    void release_unused_prebuilt(const NeighbourPiece& upload) noexcept;
    // The piece's terrain commands, placed and fog culled (renderer_neighbour_maps.cpp).
    bool submit_neighbour_terrain(const NeighbourPiece& piece,const std::array<float,16>& world_to_clip,
        NativeGeometrySubmitFn submit,void* context,const char*& failure);
    void retire_dedicated_geometry() noexcept;
    bool dedicated_geometry_stale_ = false; // released neighbours may leave dedicated meshes
    bool dedicated_geometry_remaining_ = false; // the last sweep left a dedicated mesh in place
    std::uint64_t dedicated_geometry_release_frame_ = 0,dedicated_geometry_sweep_frame_ = 0;
    static constexpr std::uint64_t kDedicatedFollowFrames = 3,kDedicatedSweepFrames = 30;
    HRESULT render_neighbour_maps(const FixedState& frame,const std::array<float,16>& world_to_clip,bool ordered) noexcept;
    HRESULT adopt_native_static_draws(NativeStaticSet& set,std::uint64_t key,std::size_t first,bool& registered,
                                     bool fixed_map_order = false);
    HRESULT flush_native_static_set(NativeStaticSet& set,const FixedState& frame);
    NativeStaticSet* native_static_set_for_layout(unsigned slot) noexcept;
    void clear_world_map_preview_gpu() noexcept;
    bool register_world_map_preview_gpu(std::uint32_t section, const std::array<float,16>& matrix,
                                       bool discovered, const FixedState& state, std::string& failure);
    const FixedState* native_static_frame_ = nullptr;   // set only during a static flush
    // During a static flush: reuse the layout's bake (nothing is queued), or
    // build and bake it (the queue holds exactly the set's draws).
    bool native_static_reuse_ = false, native_static_bake_ = false;
    // Bumped whenever a record or mesh a static set may hold changes in
    // place (mesh relocation or geometry, prepared-handle or pose reset); a
    // static bake is valid only for the value it was built at.
    std::uint64_t native_static_content_revision_ = 1;
    std::uint64_t native_static_revision_serial_ = 1;   // last set revision issued (sets start at 1)
    void native_static_content_changed() noexcept { ++native_static_content_revision_; }
    HRESULT bake_native_static_layout(NativeSubmissionLayout& layout);
    HRESULT upload_native_static_work(NativeSubmissionLayout& layout);
    // Neighbour-map liquids wait for the end of the world's opaque work
    // (composite_world), so the frame's one water scene copy is taken after
    // every figure (wading legs show through the water).
    bool neighbour_liquids_pending_ = false;std::uint64_t neighbour_liquids_frame_index_ = 0;
    FixedState neighbour_liquids_state_{};std::array<float,16> neighbour_liquids_clip_{};
    std::uint64_t native_static_generation_ = 1;
    Microsoft::WRL::ComPtr<ID3D11Buffer> native_terrain_frame_constants_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> native_terrain_frame_srv_;
    // Terrain value-noise lattice (terrain_material.h terrainNoise): 1024 x
    // 1024 R8, made once with the native pipeline; bound to the pixel stage
    // with the terrain frame (t122, wrap sampler s12).
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> terrain_noise_srv_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> terrain_noise_sampler_;
    // The active map's saved-layout origin on whole tiles (nativeContactLayout
    // of its batches; 0,0 when the layout does not list it), per map.
    std::array<float,2> terrain_layout_origin_{};
    std::string terrain_layout_name_; // its layout name (neighbour_map_name); empty: not in the layout
    std::uint64_t terrain_layout_origin_generation_=UINT64_MAX;
    std::vector<float> native_terrain_layer_shifts_;
    std::uint64_t native_terrain_layer_shift_revision_ = 0;
    UINT native_terrain_layer_shift_capacity_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> native_terrain_layer_shift_buffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> native_terrain_layer_shift_srv_;
    std::uint64_t native_layout_revision_ = 1;
    UINT dynamic_vertex_capacity_ = 0;
    UINT dynamic_index_capacity_ = 0;
    UINT dynamic_vertex_offset_ = 0;
    UINT dynamic_index_offset_ = 0;
    bool discard_vertices_ = true;
    bool discard_indices_ = true;
    UINT native_dynamic_capacity_ = 0;
    UINT native_draw_work_capacity_ = 0;
    std::uint64_t native_preparation_epoch_ = 0;
    UINT native_preparation_count_ = 0;
    UINT native_visible_triangle_capacity_ = 0;
    UINT native_prepared_vertex_capacity_ = 0;
    UINT native_world_vertex_capacity_ = 0, native_world_vertex_used_ = 0;
    bool native_world_reclaim_pending_ = false;
    UINT native_indirect_args_capacity_ = 0;
    std::uint64_t native_geometry_live_bytes_ = 0;
    std::unordered_map<std::uint64_t, ComPtr<ID3D11RasterizerState>> raster_states_;
    // Borrowed from raster_states_ only while a clipped draw is executing.
    ID3D11RasterizerState* camp_unclipped_raster_ = nullptr;
    bool camp_raster_failure_reported_ = false;
    std::unordered_map<std::uint64_t, ComPtr<ID3D11DepthStencilState>> depth_states_;
    std::unordered_map<std::uint64_t, ComPtr<ID3D11BlendState>> blend_states_;
    std::unordered_map<std::uint64_t, ComPtr<ID3D11SamplerState>> sampler_states_;
    ID3D11SamplerState* applied_sampler_ = nullptr;  // apply_state's s0 (owned by sampler_states_)
    FramePlan frame_plan_;  // this frame's passes of the converted file group (frame_plan.h)
    std::unordered_map<std::uint64_t, OverlayTexture> overlay_textures_;
    std::unordered_map<NativeTextureArrayKey, std::vector<NativeTextureArrayPage>,
                       NativeTextureArrayKeyHash> native_texture_arrays_;
    std::unordered_map<std::string, NativeOwnedTexture> native_owned_textures_;
    // Material classes (renderer_materials.cpp, pseudo-PBR S1): per texture
    // value (backing) the composed character atlas' profile, built from the
    // latest layer list of its name; the class library loads once.
    struct MaterialProfileGpu {
        std::string name;          // the game texture name (composition key)
        std::uint64_t serial = 0;  // MaterialComposition::serial it was built from
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11ShaderResourceView> relief_srv;  // R8 relief strength (t38)
        // Each texel's class index (R8_UINT, MaterialLibrary::build_profile)
        // and whether any texel is a strand class (fur armour).
        ComPtr<ID3D11ShaderResourceView> class_srv;
        // D3D12: the profile (slice 0) and the relief in red (slice 1), one
        // two-slice array with a CPU-built mip chain; each draw names it by
        // one bindless index (instance padding[1]).
        D3D12TextureRef profile12;
        // D3D12: the class texture (R8_UINT, as class_srv), made only for an
        // atlas with a strand class: the fur pass reads it for fur armour.
        D3D12TextureRef class12;
        bool strands = false;
        bool coat = false;
        bool failed = false;       // no usable layer: the atlas stays unclassified
        bool unit = false;         // drawn on a unit: a fixed texture takes units.json
        // D3D12: keyed by a client surface itself (no owned-texture entry), so
        // it goes when that surface retires (retire_surface), not by trim.
        bool surface = false;
    };
    std::unordered_map<std::uint64_t, MaterialProfileGpu> material_profiles_;
    std::shared_ptr<MaterialLibrary> material_library_;
    // D3D12: bumped whenever a profile appears, goes or the level changes;
    // the flush then refreshes every record's profile index once.
    std::uint64_t material_profiles_revision_ = 0, material_profiles_indexed_ = UINT64_MAX;
    // Terrain 2.0 roughness by cell class (S3, load_terrain_roughness) and the
    // values the material cache bakes from (crevice strength, roughness).
    std::array<float,8> terrain_roughness_{};std::array<float,9> terrain_surface_bake_{};bool terrain_roughness_tried_=false;
    bool material_library_tried_ = false;
    std::string material_notice_;
    std::uint64_t material_profiles_built_ = 0;
    std::uint64_t material_checked_revision_ = 0;
    bool material_profiles_dirty_ = false;
    int material_prepared_level_ = -1;
    bool material_prepared_metal_mask_ = true;
    int material_level() const noexcept;
    // The fur level in effect: F10 fur_mode, 0 at material_quality 0 (user
    // decision 2026-11-08: no fur at all there; the coats need the material
    // classes to keep off horn and mouths).
    int fur_mode() const noexcept;
    void note_material_texture(const std::string& name, std::uint64_t backing, bool unit, bool surface = false) noexcept;
    void prepare_material_profiles() noexcept;
    ID3D11ShaderResourceView* material_profile_srv(std::uint64_t backing) noexcept;
    ID3D11ShaderResourceView* material_relief_srv(std::uint64_t backing) noexcept;
    // The atlas' class texture (null when it has none) and whether it holds
    // a strand class; the strand classes as a 256-bit mask (fur pass).
    ID3D11ShaderResourceView* material_class_srv(std::uint64_t backing, bool& strands, bool& coat) noexcept;
    const D3D12Texture* material_class_d3d12(std::uint64_t backing, bool& strands, bool& coat) noexcept;
    std::array<std::uint32_t, 8> material_strand_classes() const noexcept;
    std::array<std::uint32_t, 64> material_coat_classes() const noexcept;
    // A batch draws with its profile at t37/t38 (D3D11; on D3D12 each draw names its own, material_profile_index_d3d12).
    bool material_profile_bound(std::uint64_t backing) noexcept {
        return !d3d12_ && material_profile_srv(backing) && material_level() > 0;
    }
    void trim_material_profiles() noexcept;
    // D3D12: the bindless index of the profile of a texture value (0: none or materials off).
    std::uint32_t material_profile_index_d3d12(std::uint64_t backing) noexcept;
    void refresh_material_profile_indices_d3d12() noexcept;
    HRESULT upload_material_profile_d3d12(MaterialProfileGpu& entry, std::uint32_t side,
                                          const std::vector<std::uint8_t>& pixels, const std::vector<std::uint8_t>& relief) noexcept;
    // Written only by the renderer's owner thread; the game thread's detach
    // retainer (retain_detached_surface_texture, under the retained scene's
    // lock) reads it while the scene thread may write: both hold this lock.
    mutable std::mutex native_owned_textures_lock_;
    std::uint64_t native_owned_texture_serial_ = 0;
    std::uint64_t shared_texture_rewrites_ = 0; // detach_shared_surface_texture copies
    std::uint64_t native_owned_texture_uploads_ = 0, native_owned_array_uploads_ = 0;
    // While a neighbour map uploads: pool serial at its start (0 = off), and the
    // distinct older textures it reused instead of uploading again.
    std::uint64_t native_texture_share_floor_ = 0, native_texture_shared_ = 0, native_texture_shared_bytes_ = 0;
    std::uint64_t native_prepared_mesh_hits_ = 0;
    std::unordered_map<NativeMeshKey, NativeMesh, NativeMeshKeyHash> native_meshes_;
    std::vector<NativePreparedMeshSlot> native_prepared_mesh_slots_;
    std::vector<NativePreparedMaterialSlot> native_prepared_material_slots_;
    std::size_t native_prepared_mesh_reuse_cursor_ = 0;
    std::size_t native_prepared_material_reuse_cursor_ = 0;
    // Highest generation of a slot trimmed off the end (trim_native_tables):
    // a slot appended at that index again starts above it, so a stale handle
    // to the trimmed slot can never match its new occupant.
    std::uint32_t native_prepared_mesh_generation_floor_ = 1;
    std::uint32_t native_prepared_material_generation_floor_ = 1;
    std::uint64_t native_prepared_mesh_reuse_sweep_frame_ = 0;
    std::uint64_t native_prepared_material_reuse_sweep_frame_ = 0;
    std::uint64_t native_world_prepared_vertices_ = 0;
    std::uint64_t native_world_reused_records_ = 0;
    std::uint64_t native_contribution_rejects_ = 0;
    std::uint64_t native_queue_epoch_ = 1;
    std::uint64_t native_mesh_resident_bytes_ = 0;
    // Mesh cache lifetime (trim_native_mesh_cache): the world epoch the cache
    // follows, the frame it began, a pending sweep of the older epoch's
    // geometry, the older meshes that sweep found still owned, and prepared
    // mesh slots handed back by expired owners.
    std::uint64_t native_cache_world_epoch_ = 0;
    std::uint64_t native_cache_epoch_frame_ = 0;
    bool native_cache_epoch_sweep_pending_ = false;
    // The queue epoch the current world began at, and its pending texture sweep.
    std::uint64_t native_texture_epoch_queue_ = 0;
    bool native_texture_epoch_sweep_pending_ = false;
    std::uint64_t native_cache_older_owned_meshes_ = 0;
    std::uint64_t native_released_mesh_slots_ = 0;
    std::uint64_t native_geometry_growths_ = 0;      // general page growths and new pages
    std::uint64_t native_cache_seen_growths_ = 0;
    // World/effect input and resource use belong to each engine draw invocation,
    // including draws without a flip. Public presentation stamps count only
    // actual swap-chain Present calls.
    std::uint64_t native_frame_index_ = 0;
    std::uint64_t presentation_frame_index_ = 0;
    SurfaceTextureResolver surface_texture_resolver_ = nullptr;
    void* surface_texture_resolver_context_ = nullptr;
    bool apply_native_lighting(const NativeWorldLighting& lighting);
    NativeWorldLighting native_world_lighting_{};
    WorldMapPreview world_map_preview_;
#ifdef EID3D11_EMBEDDED
    bool embedded_world_map_preview_option_ = false;
    std::uint64_t embedded_world_map_preview_sources_ = 0;
#endif
    std::uint64_t world_map_preview_generation_ = 0;
    std::vector<std::string> world_map_preview_reasons_;
    bool native_world_cave_ = false;  // the zone is a cave (set_native_world_lighting)
    // Cave irradiance volume (renderer_point_shadows.cpp).
    float native_world_hour_ = 12.f;  // game hour of the world light (cave exit colour)
    int native_world_region_ = 0;     // fog_region() of the world light
    // Volume grids: L1 red, green, blue.
    ComPtr<ID3D11Texture3D> cave_volume_[3];
    ComPtr<ID3D11ShaderResourceView> cave_volume_srv_[3];
    ComPtr<ID3D11UnorderedAccessView> cave_volume_uav_[3];
    ComPtr<ID3D11ComputeShader> cave_inject_cs_;
    // D3D12 backend: the grids, the bounce buffers and the exits as executor
    // resources (update_cave_irradiance's D3D12 branch; same capacities).
    struct CaveD3D12 {
        std::vector<ShaderProgram> programs;  // append_cave_irradiance_shader_programs: inject, glow VS, glow PS
        D3D12TextureRef volume[3];
        D3D12BufferRef sources, grid, vpls, exits;
    } cave12_;
    // The lamps' virtual lights (cave_lamp_virtual_lights), demand-sized,
    // their key (sources, height grid, count) and how many lamps found ground.
    ComPtr<ID3D11Buffer> cave_vpl_buffer_;
    ComPtr<ID3D11ShaderResourceView> cave_vpl_srv_;
    UINT cave_vpl_capacity_ = 0;
    std::vector<CaveVirtualLight> cave_vpls_;
    std::uint64_t cave_vpl_key_ = 0;
    // The cave exit glow pass (kCaveGlowShader).
    ComPtr<ID3D11VertexShader> cave_glow_vs_;
    ComPtr<ID3D11PixelShader> cave_glow_ps_;
    ComPtr<ID3D11Buffer> cave_glow_constants_;
    ComPtr<ID3D11DepthStencilState> cave_glow_depth_;
    ComPtr<ID3D11BlendState> cave_glow_blend_;
    ComPtr<ID3D11RasterizerState> cave_glow_raster_;
    bool cave_glow_pending_ = false, cave_glow_failed_ = false;
    // The cave exits (CaveExitGpu) for the glow pass and the border fog (PS t84).
    ComPtr<ID3D11Buffer> cave_exit_buffer_;
    ComPtr<ID3D11ShaderResourceView> cave_exit_srv_;
    UINT cave_exit_capacity_ = 0, cave_exit_count_ = 0;
    UINT cave_vpl_lit_ = 0;
    std::vector<NativePointLightValue> cave_sources_uploaded_;  // the sources in the GPU buffers
    UINT cave_sources_uploaded_capacity_ = 0;
    PointLightGrid cave_grid_;                    // their bounce grid
    bool cave_volume_heights_ = false;            // the last update had the height grid
    ComPtr<ID3D11Buffer> cave_inject_constants_;
    // Bounce sources (point lights + unpublished emissive clusters), their
    // bounce-reach grid and the TEMPORARY per-source hit readback (#208).
    ComPtr<ID3D11Buffer> cave_source_buffer_, cave_grid_buffer_;
    ComPtr<ID3D11ShaderResourceView> cave_source_srv_, cave_grid_srv_;
    UINT cave_source_capacity_ = 0, cave_grid_capacity_ = 0;
    std::vector<NativePointLightValue> cave_sources_;
    ComPtr<ID3D11SamplerState> cave_volume_sampler_;
    std::array<std::uint32_t,3> cave_volume_size_{};
    std::array<float,3> cave_volume_origin_{};
    float cave_volume_cell_ = 0.f;
    std::uint32_t cave_volume_slab_ = 0;        // next round-robin slab
    std::uint64_t cave_volume_frame_ = UINT64_MAX; // the frame of the last update
    int cave_volume_quality_ = 0;               // quality of the last full fill
    bool cave_volume_outdoor_ = false;          // the sky grid holds terrain occlusion
    std::uint64_t cave_volume_map_generation_ = UINT64_MAX;
    // Cave exits of the active map (map.txt) and the region's outdoor light
    // table (lights<region>.ini), read once per map / region.
    std::vector<CavePortalRect> cave_portals_;
    std::vector<std::array<float,3>> cave_mushrooms_;  // giant mushroom places (glow sources)
    OutdoorLightTable cave_light_table_;
    int cave_light_table_region_ = -1;
    std::array<float,3> cave_terrain_low_{}, cave_terrain_high_{};
    bool cave_volume_active_ = false, cave_volume_failed_ = false;
    CaveSampleConstants native_cave_constants_written_{};
    bool native_point_constants_dirty_ = true;  // the point-light constants need a rewrite
    bool native_world_lighting_valid_ = false;
    bool native_world_lighting_dirty_ = false;
    std::uint64_t native_world_lighting_uploads_ = 0;
    using NativePointLightGpu=NativePointLightValue;
    static_assert(sizeof(NativePointLightGpu)==64,"point-light GPU layout" );
    std::vector<NativePointLightGpu> native_point_lights_;
    UINT native_point_light_capacity_ = 0;
    PointLightGrid native_point_light_grid_;
    std::vector<std::array<float,4>> native_point_light_grid_inputs_;
    UINT native_point_light_grid_capacity_ = 0;
    bool native_point_lights_dirty_ = false;
    std::int64_t last_present_tick_ = 0;
    double present_interval_ms_ = 0.0;
    double present_smoothed_ms_ = 0.0;
    // Vertical output scale owned pane text was last composed at (#185).
    float observed_text_scale_ = 0.f;
    unsigned text_scale_mismatch_reports_ = 0;
    std::array<float, 4> debug_projection_coefficients_{};
    bool debug_projection_valid_ = false;
    DebugResourceViewWorkSnapshot debug_resource_view_work_{};
    FrameStatistics frame_statistics_;
    SystemTelemetrySampler system_telemetry_;
    SystemTelemetrySnapshot system_telemetry_snapshot_{};
    bool system_telemetry_active_ = false;
    std::int64_t perf_draw_ticks_ = 0;
    std::int64_t perf_blit_ticks_ = 0;
    std::int64_t perf_present_ticks_ = 0;
    std::uint64_t perf_frames_ = 0;
    std::uint64_t perf_draw_calls_ = 0;
    std::uint64_t perf_blit_calls_ = 0;
    std::uint64_t perf_upload_misses_ = 0;
    std::uint64_t perf_upload_bytes_ = 0;
    std::uint64_t perf_native_draws_ = 0;
    std::uint64_t perf_native_batches_ = 0;
    std::uint64_t perf_native_compute_dispatches_ = 0;
    std::uint64_t perf_native_vertices_ = 0;
    std::uint64_t perf_native_dynamic_bytes_ = 0;
    std::uint64_t perf_native_gpu_lit_draws_ = 0;
    std::uint64_t perf_native_mesh_hits_ = 0;
    std::uint64_t perf_native_mesh_misses_ = 0;
    std::uint64_t perf_native_mesh_evictions_ = 0;
    // Temporary (camera-motion churn): miss classes, reported per present in
    // camera_path lines.
    std::uint64_t perf_native_mesh_content_misses_ = 0;
    std::uint64_t perf_native_mesh_world_misses_ = 0;
    std::uint64_t native_generic_ingress_calls_ = 0;
    std::uint64_t native_prepared_ingress_calls_ = 0;
    struct NativeFrameStats : RendererResourceSnapshot {
        PlacedHitchRendererFrame::ColorResourceDiagnostic color_resource{};
        PlacedHitchShadowDecision shadow_decision{};
        std::uint64_t native_depth_cold_handle_owner_identity = 0;
        std::uint64_t native_depth_cold_source_revision = 0;
        std::uint64_t native_depth_cold_inherited_context = 0;
        std::uint64_t native_depth_cold_caster_revision = 0;
        std::uint64_t native_depth_cold_world_allocation_generation = 0;
        std::uint64_t native_depth_cold_pose_revision_generation_allocation = 0;
        std::uint64_t native_depth_cold_animation_generation_ownership = 0;
        std::uint64_t native_depth_cold_bounds = 0;
    } native_frame_stats_{};
    PlacedHitchRendererFrame::ColorResourceDiagnostic* active_color_resource_ = nullptr;
    bool color_mesh_active_ = false;
    bool color_texture_active_ = false;
    bool color_cold_active_ = false;
#ifdef EID3D11_TESTING
    // Artificial capacity injection is absent from production builds.
    std::uint64_t test_native_owned_texture_budget_ = UINT64_MAX;
    std::uint64_t test_native_shadow_budget_ = UINT64_MAX;
    std::uint64_t test_shadow_history_epoch_ = 0;
    std::uint8_t test_native_flush_failure_ = 0;
    UINT test_native_enqueue_failure_ = 0;
    bool test_native_world_allocation_failure_ = false;
    std::uint64_t test_native_world_allocation_attempts_ = 0;
    bool test_fail_native_specular_color_upload_ = false;
    bool test_color_resource_diagnostic_ = false;
    bool test_fail_character_pane_text_srv_creation_ = false;
    std::size_t test_last_character_pane_draw_calls_ = 0;
    UINT test_native_geometry_buffer_failures_ = 0;
    UINT test_native_geometry_view_failures_ = 0;
    UINT test_native_geometry_page_limit_ = 0;
    UINT test_native_geometry_upload_failure_ = 0;
    UINT test_native_draw_work_buffer_failures_ = 0;
    UINT test_native_draw_work_view_failures_ = 0;
    UINT test_native_indirect_buffer_failures_ = 0;
    UINT test_native_indirect_view_failures_ = 0;
    std::array<UINT, 2> test_native_work_prefix_{};
    std::uint64_t test_native_prepared_mesh_slot_visits_ = 0;
    std::uint64_t test_native_prepared_material_slot_visits_ = 0;
#endif
    RenderProfiler profiler_;
    PlacedHitchProfile placed_hitch_profile_;
    std::string placed_hitch_scene_json_;
    std::int64_t placed_hitch_previous_trim_ticks_ = 0;
    bool placed_hitch_previous_trim_valid_ = false;
    void record_placed_hitch_frame(
        std::int64_t present_entry, std::int64_t present_return,
        bool focused, HRESULT present_result,
        const OuterFrameTimingSnapshot& outer_timing) noexcept;
    void finish_placed_hitch_profile(PlacedHitchFinishReason reason) noexcept;
    bool export_placed_hitch_profile() noexcept;
    DebugOverlay debug_overlay_;
    // File-driven test harness (test_harness.h): the renderer-owned commands.
    enum class HarnessRequest : std::uint8_t { none, capture, profile };
    HarnessRequest step_test_harness(const ScenePresentInputs* inputs) noexcept;
    bool apply_harness_config(const HarnessCommand& command, std::string& detail) noexcept;
    bool write_harness_screenshot(const std::string& path, std::string& detail) noexcept;
    bool write_hudless_image(const std::string& path, std::string& detail) noexcept;
    bool write_profile_image_d3d12(const std::string& path, std::string& detail) noexcept;
    void capture_shadow_temporal_boundary(DWORD flags, DWORD rectangle_count, bool full_surface);
    std::string harness_unready_parts(std::string& ready_detail) noexcept;
    // Load reveal: after a new world epoch the first world frames keep the
    // last loading picture on screen until the map, its neighbours and its
    // ground cover are prepared (or a cap passes), so nothing pops in after
    // the loading screen. Frames keep running (multi-frame preparation needs
    // them); only their presentation is replaced. RENDERER_ARCHITECTURE.md.
    void apply_load_reveal() noexcept;
    // Which cap a held frame counts against: the neighbour stream's or the rest.
    enum class LoadRevealWait { other, neighbours };
    const char* load_reveal_waiting_on(LoadRevealWait& kind) noexcept;
    // Also drains neighbours and paints every frame. Written by the presenting
    // thread, read by the game thread (load_cursor_wanted): atomic.
    std::atomic<bool> load_reveal_holding_{false};
    bool load_reveal_tracking_ = false;     // timing the current load (hold on or off)
    bool load_picture_this_frame_ = false;  // present_image ran in this frame
    std::uint64_t load_reveal_epoch_ = 0, load_reveal_draws_ = 0, load_reveal_revealed_epoch_ = 0;
    std::int64_t load_reveal_first_world_ = 0, load_reveal_last_world_ = 0;
    // The hold caps' clocks (each held frame adds at most 250 ms): frames held
    // for the neighbour stream, and frames held for every other wait.
    double load_reveal_held_ms_ = 0, load_reveal_neighbour_held_ms_ = 0;
    unsigned load_reveal_frames_ = 0;
    // Held frames per readiness reason (static strings), for the reveal line.
    std::array<std::pair<const char*, unsigned>, 12> load_reveal_waits_{};
    std::uint64_t load_reveal_va_peak_ = 0;
    ComPtr<ID3D11Texture2D> load_picture_;  // GPU copy of the last loading picture frame
    D3D12TextureRef load_picture12_;         // renderer_backend=d3d12: the same copy (apply_load_reveal)
    // Load progress during the hold (renderer_load_progress.cpp): the game's
    // progress movie steps one frame per load progress point and never shows
    // its last frame (005DA400). While the picture is held, the frames after
    // the one shown follow the hold's real progress (load_reveal_progress),
    // decoded from the same movie and composed as the game's picture.
    struct LoadProgressMovie {
        std::string path;                    // as the game opened it
        unsigned width = 0, height = 0, frames = 0, shown = 0; // shown: 0-based index of the held frame
        bool attempted = false;              // decode tried for this load (once)
        std::vector<SurfaceImage> after;     // frames shown + 1 .. frames - 1, GPU textures
        float position = 0;                  // frames past `shown`, never decreasing
        std::shared_ptr<ProgressMovieFrames> source;
        // Typed hosts decode at most the two adjacent progress frames.
        std::array<SurfaceImage, 2> images;
        std::array<unsigned, 2> image_frames{UINT_MAX, UINT_MAX};
        bool source_failed = false;
    } load_progress_movie_;
#ifdef EID3D11_EMBEDDED
    std::shared_ptr<const SurfaceImage> embedded_movie_image_;
#endif
    bool load_progress_movie_valid_ = false; // the latest loading picture was a progress frame
    // Per component (world, neighbours, critter perches, shader sets, ground
    // cover, warm-up): the most work units seen outstanding during this hold.
    std::array<std::size_t, 6> load_reveal_progress_peak_{};
    bool load_reveal_perches_ready_ = false;
    bool load_picture_fitted_ = false;      // present_image composed a full-screen picture this frame
    void capture_load_progress_movie(bool fitted) noexcept;
#ifndef EID3D11_EMBEDDED
    bool decode_load_progress_movie() noexcept;
#endif
    // Scene mode: the game's progress movie state as the game thread read it
    // at the latest fitted picture (present_image); the scene thread's
    // capture_load_progress_movie uses it instead of game memory.
    ProgressMovie progress_movie_input_;
    bool progress_movie_input_valid_ = false;
    // Scene mode: the movie decode runs on the game thread (the game's own
    // binkw32 imports, which only the game thread calls). The scene thread
    // files a request; queue_present decodes it and leaves the frames here.
    struct LoadProgressDecode {
        std::uint64_t serial = 0;            // 0: no request
        bool taken = false, done = false;
        std::string path;
        unsigned width = 0, height = 0, frames = 0, shown = 0;
        std::vector<SurfaceImage> after;     // done: the frames (empty: failed, reported)
    };
    std::mutex load_progress_decode_lock_;   // guards the two members below
    LoadProgressDecode load_progress_decode_;
    std::uint64_t load_progress_decode_serials_ = 0;
#ifndef EID3D11_EMBEDDED
    void service_load_progress_decode() noexcept; // game thread, queue_present
#endif
    // Core mode: the host's progress-movie decode request and its answer.
    bool core_take_progress_request(std::string& path, unsigned& width, unsigned& height, unsigned& frames,
                                    unsigned& shown, std::uint64_t& serial) noexcept;
    void core_progress_frames(std::uint64_t serial, unsigned width, unsigned height,
                              const std::vector<std::vector<std::uint8_t>>& frames, const char* failure) noexcept;
    bool core_host_ = false;                 // host, core mode: this Renderer is the core-mode host's
    // Host, core mode: the game window was in front at the game's present
    // (scene_core::Present). The host's own window is a child of another
    // process's top-level window, so GetForegroundWindow never names it.
    bool core_foreground_ = true;
    bool window_in_front() const noexcept {
        return core_host_ ? core_foreground_ : (!hwnd_ || GetForegroundWindow() == hwnd_);
    }
    std::uint64_t core_progress_seen_ = 0;   // game process: the host decode request last served
    void cancel_load_progress_decode() noexcept;
    void draw_load_progress(float progress) noexcept;
    float load_reveal_progress() noexcept;  // 0..1 of the work outstanding during this hold, done
    const char* load_reveal_world_wait() noexcept;
    const char* load_reveal_ground_cover_wait() noexcept;
    std::size_t native_specialisation_programs_remaining() const noexcept;
    // Pipeline warm-up (renderer_warmup.cpp): during held frames, one tiny
    // draw per shader combination and target formats a later first draw
    // would otherwise make the driver compile mid-play (specialised sets'
    // siblings, liquids, water reflection, particles, point shadows), into
    // 4x4 scratch targets; the hold waits until the GPU has run them. The
    // list is rebuilt each held frame; warmup_signature_ restarts it when
    // its programs change (a set arrives, the key changes, a new device).
    void pump_pipeline_warmup() noexcept;
    void reset_pipeline_warmup() noexcept;
    bool warmup_done_ = false, warmup_query_pending_ = false;
    std::uint64_t warmup_signature_ = 0;
    std::size_t warmup_next_ = 0, warmup_items_ = 0;
    unsigned warmup_draws_ = 0;            // draws issued for the current load (load_reveal line)
    ComPtr<ID3D11Query> warmup_query_;
    std::vector<std::pair<DXGI_FORMAT, ComPtr<ID3D11RenderTargetView>>> warmup_targets_;
    std::vector<std::pair<DXGI_FORMAT, ComPtr<ID3D11DepthStencilView>>> warmup_depths_;
    unsigned harness_stage_ = 0;
    ShadowTemporalPresentOutcome harness_shadow_temporal_outcome_ = ShadowTemporalPresentOutcome::none;
    unsigned long harness_completed_entry_ = 0; // scene thread: last completed queued command
    std::uint64_t harness_started_ms_ = 0;
    unsigned harness_wait_ = 0;
    // camera_path (temporary): the previous present's counter, the present
    // count and the last profiler frame written.
    std::int64_t harness_path_previous_=0;unsigned harness_path_present_=0;std::uint64_t harness_path_profile_frame_=0;
    std::array<std::uint64_t,4> harness_path_cache_counters_{};
    // Frame accounting (frame_accounting.h): the camera_path row of this
    // present waits for the frame's end; the game thread's last closed frame;
    // D3D counters and shadow decision counts at the previous row; buffered rows.
    std::string harness_path_line_;
    frame_acct::Record fa_game_frame_{};
    std::array<std::int64_t,frame_acct::counter_count> fa_counters_{};
    decltype(ShadowRendererDiagnostics::counts) fa_shadow_witness_{};
    std::string harness_path_rows_;
    void finish_harness_path_row(const frame_acct::Record& game_frame,const frame_acct::Record* scene_frame) noexcept;
    void write_harness_path_rows() noexcept;
    std::string harness_capture_directory_;
    std::string harness_overlay_shot_;   // screenshot path waiting for this present's overlay pass
    bool harness_profile_armed_ = false;

    // ---- x64 host core (S2f, Host/core_host.cpp) ----
public:
    // The host's one device-creation function made device and context; the
    // Renderer adopts them without a window (the host presenter owns present).
    // Idle until the host feeds it records (S3): no targets, shaders or jobs.
    HRESULT adopt_host_device(ID3D11Device* device, ID3D11DeviceContext* context, UINT width, UINT height) noexcept;
    // x64 host, renderer_backend=d3d12 (before ensure_host): the frame goes
    // to this executor; the adopted D3D11 device only answers adapter queries.
    void set_d3d12_frame(D3D12Frame* frame) noexcept { d3d12_ = frame; }
    bool d3d12_backend() const noexcept { return d3d12_ != nullptr; }
    bool d3d12_selected() const noexcept {
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
        return d3d12_backend() || embedded_d3d12_failed_;
#else
        return d3d12_backend();
#endif
    }
    // D3D12 backend: a pass this backend does not draw yet, named once (F11 list).
    void d3d12_omit(const char* name, bool world) noexcept;
private:
    // Constant/structured ring support of the adopted or created device.
    void detect_device_options() noexcept;
    // ---- x64 host, renderer_backend=d3d12 (d3d12_frame.h) ----
    // Null in the x86 DLL and on the D3D11 host: every branch on it is host-only.
    D3D12Frame* d3d12_ = nullptr;
    HRESULT ensure_host_d3d12(HWND window, UINT width, UINT height);
    HRESULT present_frame_d3d12(const ScenePresentInputs* inputs);
    HRESULT render_pending_blits_d3d12();
    HRESULT upload_d3d12(SurfaceImage& image, SurfaceUploadResult* result);
    // D3D11 GenerateMips of a B8G8R8A8_UNORM texture, on the CPU: `level`
    // (tight width x height texels) becomes level 0, each further level the
    // rounded 2x2 box of the one above in stored (UNORM) space, alpha
    // included, clamped at odd edges. Writes `levels` levels; consumes `level`.
    HRESULT write_box_mips_d3d12(D3D12Texture& texture, std::vector<std::uint32_t>& level, UINT width, UINT height, UINT levels);
    // A static world surface's full mip chain from its CPU pixels (the
    // converted level its single-level upload writes): into `texture` when it
    // already has that layout, else a new texture. Uncompressed surfaces only.
    HRESULT surface_mips_d3d12(SurfaceImage& image, D3D12TextureRef& texture);
    HRESULT draw_screen_rectangle_d3d12(const ScreenRectangleRequest& request, const D3DVIEWPORT7& viewport, float grid_step = 0.f);
    D3D12TextureRef d3d12_white_;  // the one white texel screen rectangles sample
    // The world target (R11G11B10 colour, D24S8 depth: the D3D11 world
    // formats), bound from the engine frame's start to its composite.
    D3D12TextureRef world_d3d12_;
    // The occlusion pyramid on D3D12 (build_native_hiz_d3d12): the D3D11
    // native_hiz_ layout (R32_FLOAT, half the world depth, full chain) from
    // the world depth after opaque terrain; the native_hiz_* sizes, frame and
    // clip are shared with D3D11.
    D3D12TextureRef native_hiz12_;
    std::vector<ShaderProgram> native_hiz12_programs_;  // append_hiz_shader_programs: copy, reduce
    HRESULT build_native_hiz_d3d12() noexcept;
    std::vector<float> native_terrain_frame_d3d12_;  // NativeTerrainFrameValues (t127), uploaded by the D3D12 flush
    bool world_d3d12_bound_ = false;
    HRESULT bind_world_d3d12();
    HRESULT composite_world_d3d12();
    bool ground_marks_under_water_d3d12() const noexcept;
    // The world pass's targets (the world, and the AO ambient share at slot 2
    // while AO runs this frame): every world draw returns to them.
    // history: the temporal sun-shadow history at slot 1 (the D3D11 flush's
    // write_history: opaque/cutout world batches and the ground cover).
    void bind_world_targets_d3d12(bool history = false) noexcept;
    // The temporal sun-shadow history on D3D12 (create_shadow_history_targets'
    // D3D11 resources): two R8 visibility targets (ping-pong, slot 1 of the
    // world pass), the previous world depth (a depth-only D24S8 target) and
    // the b6 values prepare_shadow_history wrote. The frame, epoch and index
    // state is the D3D11 one (shadow_history_*).
    std::array<D3D12TextureRef,2> shadow_history_mask12_;
    D3D12TextureRef shadow_history_depth12_;
    std::array<std::uint8_t,96> shadow_history_constants12_{};
    bool world_history_bound12_ = false;  // the last bind_world_targets_d3d12 bound slot 1
    HRESULT ensure_shadow_history_d3d12();
    // b6, t20/t21 and s3 for a pass that reads the history (active: the
    // prepare_shadow_history result; inactive binds none and zero b6).
    void bind_shadow_history_d3d12(bool active) noexcept;
    // Ambient occlusion on D3D12 (renderer_native_d3d12.cpp): the
    // render_ambient_occlusion passes over the executor's targets.
    struct AmbientOcclusionD3D12 {
        std::vector<ShaderProgram> programs;  // append_ambient_occlusion_shader_programs order
        D3D12TextureRef ambient, raw, filtered, depth;
        UINT divisor = 0, failed_width = 0, failed_height = 0, failed_divisor = 0;
        std::uint64_t frame = UINT64_MAX;  // the world frame whose share is bound (cleared at its start)
    } ao12_;
    HRESULT update_ambient_occlusion_d3d12();
    HRESULT ambient_occlusion_d3d12();
    // Depth prepass and sun-shadow mask on D3D12 (the D3D11 prepass_rest_,
    // prepass_normal_ and shadow_mask_ at the world target's size). No
    // stencil mark: the receiver record is cleared before each prepass, so
    // the mask pass leaves every pixel no prepass wrote at once.
    D3D12TextureRef prepass_rest12_, prepass_normal12_, shadow_mask12_;
    // Their DXIL slots (native_shader.h nativeReceiverRest .. nativeShadowMask,
    // and the cover's gcShadowMask): t1/t2/t4/t5 are vertex-stage buffers on
    // the one D3D12 t table.
    static constexpr UINT kPrepassRestSlotD3D12 = 117, kPrepassNormalSlotD3D12 = 118,
                          kPrepassDepthSlotD3D12 = 119, kShadowMaskSlotD3D12 = 120;
    UINT prepass_failed_width12_ = 0, prepass_failed_height12_ = 0;
    HRESULT ensure_prepass_targets_d3d12();
    // Clears the receiver record and binds it with the world depth for a
    // prepass (the caller's draws, then resolve_world_shadow_mask_d3d12).
    void begin_world_prepass_d3d12() noexcept;
    // resolve_world_shadow_mask on D3D12: the mask pass over the receiver
    // record and the world depth, then the mask bound for the material pass
    // (t120). The caller has bound b1, b2, b6, b10, t9, t20, t21 and s1, s3,
    // s4, s10 as its material pass reads them, and rebinds its targets after.
    HRESULT resolve_world_shadow_mask_d3d12();
    HRESULT dim_hud_layout_d3d12();
    // The status line under the F10 overlay (both present paths).
    void update_status_notice(const char* reset_notice);
    // Native-canvas UI clip (camp cells, HUD apertures) in output pixels:
    // false when neither clips (apply_state and the D3D12 UI pass).
    bool ui_clip_rect(const widescreen::HudComposition& hud, const camp::Clip& clip, RECT& rectangle) const noexcept;
    // The queued blits' vertices (strips, or lists for batchable text).
    void write_ui_vertices(void* vertices, const std::vector<UINT>& starts) const noexcept;
    // A native batch's b0 constants (renderer_internal.h Constants, written to
    // `constants`) and contact views, for both executors.
    void native_batch_constants(const NativeBatch& batch, const NativeQueuedDraw& draw, const NativeStateBucket& bucket,
                                ID3D11ShaderResourceView* (&contact_views)[4], bool neighbour_terrain_blend,
                                bool neighbour_fog_skip, bool shadow_coverage, void* constants);
    // ---- native packets on D3D12 (Source/renderer_native_d3d12.cpp) ----
    // The native queue's records drawn by the D3D12 executor: interface
    // (SemanticKind::ui) batches now; world batches are omitted with the
    // marker until M2.
    struct NativeD3D12 {
        bool ready = false, failed = false;
        std::vector<ShaderProgram> programs;  // append_native_shader_programs: [0] VS, [1] PS, [2] build CS
        // append_terrain_tessellation_shader_programs: [0] nativeTessVS, [1] HS, [2] DS,
        // [3] HS and [4] DS without the snow trail code.
        std::vector<ShaderProgram> tessellation;
        D3D12BufferRef dynamic, work, arguments, visible, prepared, world_vertices, instances, draws;
        // Persistent (kept across growth like their D3D11 buffers): world
        // vertex ranges, world states, terrain phase animation, pose nodes;
        // the frame values: terrain frame (t127), layer shifts (t19), the
        // phase copies' work list.
        D3D12BufferRef world_states, animation, pose_nodes, terrain_frame, layer_shifts, phase_work, pose_inputs;
        // Point lights (t18), their grid (t17) and count/grid constants (b5); the
        // terrain noise lattice (t122, s12).
        D3D12BufferRef point_lights, point_light_grid;
        std::array<std::uint32_t, 16> point_light_constants{};
        D3D12TextureRef terrain_noise;
        D3D12BufferRef game_effects;  // the game effect elements of the latest draw (t7)
        std::uint32_t layer_shift_revision = 0;
        UINT instance_capacity = 0;
    } native12_;
    HRESULT native_d3d12_ensure();
    HRESULT flush_native_d3d12(unsigned layout_slot);
    HRESULT native_d3d12_buffer(D3D12BufferRef& buffer, UINT64 bytes, UINT stride, UINT flags);
    // A persistent buffer grown to at least bytes, its first keep bytes copied.
    HRESULT native_d3d12_persistent(D3D12BufferRef& buffer, UINT64 bytes, UINT stride, UINT64 keep);
    HRESULT upload_native_terrain_phases_d3d12();
    HRESULT upload_native_terrain_frame_d3d12();
    HRESULT prepare_native_poses_d3d12();
    HRESULT upload_native_point_lights_d3d12();
    // A terrain page's texture on D3D12: BC1 with the loader's shipped mip
    // chain when it matches the game surface (as the D3D11 terrain array
    // slice takes it), else the surface's own single level.
    HRESULT terrain_texture_d3d12(SurfaceImage& image, const TerrainTexturePage* page, D3D12TextureRef& output);
    struct TerrainTextureD3D12 { std::uint64_t generation = 0; D3D12TextureRef texture; bool surface_alias = false; };
    std::unordered_map<std::uint64_t, TerrainTextureD3D12> terrain_textures_d3d12_;
    // The active terrain's pages prepared at once on workers (read, parse,
    // texture and upload memory) at its first page miss; each miss then only
    // records its copies (terrain_texture_d3d12). Held with the terrain asset
    // that owns the pages, one upload per page index; released with the world.
    struct TerrainPagesD3D12 {
        std::shared_ptr<const TerrainCpuAsset> terrain;
        std::vector<D3D12PreparedUploadRef> uploads;
    } terrain_pages_d3d12_;
    void prepare_terrain_pages_d3d12(const TerrainTexturePage& page);
    HRESULT update_contact_maps_d3d12();
    // One terrain page copied into a contact atlas array (all levels from the
    // atlas size down), creating the array at the first page (D3D12 backend).
    HRESULT copy_contact_atlas_d3d12(D3D12Texture& source, UINT material, ContactBlendMaps& maps);
    // A neighbour's contact grid (with its blend rows once its families load)
    // and atlas on D3D12: what add_ground_cover_neighbour builds for its
    // terrain draws, without the cover grids.
    void add_neighbour_blend_maps_d3d12(const NeighbourPiece& upload, const std::array<float,16>& active_to_world, UINT contact_map) noexcept;
    HRESULT ensure_terrain_noise_d3d12();
    HRESULT upload_native_instances_d3d12();
    // world_mips: a world texture (as get_native_owned_texture's): a static
    // single-level uncompressed source gets its full mip chain (D3D11
    // GenerateMips there; a 2x2 box filter of the BGRA8 expansion here).
    HRESULT get_native_owned_texture_d3d12(const NativeOwnedTextureView& source, D3D12TextureRef& output,
                                           std::uint64_t& backing, NativeGeometryResult* result, bool world_mips = false,
                                           bool budget_reducible = false);
    // The world-clip constants (b3) as both executors upload them.
    void native_world_clip_values(std::array<std::uint8_t, sizeof(std::array<float, 16>) * 2 + 64>& values);
};

} // namespace owned

// Core mode: the typed-command encoder (needs the complete Renderer and
// scene_core_types.h; when that header included us, it includes this itself).
#ifndef SCENE_CORE_TYPES_DEFERRED_POST
#include "scene_core_post.h"
#endif
