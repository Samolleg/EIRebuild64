#pragma once
#include "scene_composite.h"
#include "bloom.h"
#include "sun_shafts.h"
#include "depth_of_field.h"
#include "camp_layout.h"
#include "gpu_blit_key.h"
#include "embedded_device.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <set>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <future>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "engine_bridge.h"
#include "widescreen.h"
#include "shadow_far_retention.h"
#include "debug_overlay.h"
#include "frame_limiter.h"
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
#include "terrain_bake.h"
#include "neighbour_fog.h"
#include "shader_cache.h"

namespace owned {
struct CampDecoration;

using Microsoft::WRL::ComPtr;

struct CharacterPaneTextFrame;
struct CharacterPaneGlyphAsset;
struct RetainedSceneIdentitySnapshot;
struct RetainedWorldPointLight;
struct RetainedShadowCasterValue;
struct ModelCpuAsset;
struct TerrainTexturePage;
struct AnimationClipAsset;
struct HarnessCommand;
struct NeighbourMapUpload;
struct FigureCpuAsset;

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
    std::uint64_t texture_serial = 0; // advances when the texture object changes
};

void update_surface_memory_accounting(SurfaceImage& image) noexcept;
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
#ifdef EID3D11_TESTING
void test_fail_next_surface_srv_creation() noexcept;
#endif

struct FixedState {
    std::array<DWORD, 256> render{};
    std::array<std::array<DWORD, 32>, 8> stage{};
    std::array<D3DMATRIX, 4> transform{};
    D3DVIEWPORT7 viewport{};
    D3DMATERIAL7 material{};
    FixedState();
};

enum class NativeOpacityClass : std::uint32_t { opaque, cutout, ordered };
struct NativeAlphaEvidence {
    bool texture_opaque = false;
    float diffuse_alpha = std::numeric_limits<float>::quiet_NaN();
};
bool native_texture_alpha_opaque(const NativeOwnedTextureView& texture,
                                std::uint32_t mip_filter) noexcept;
NativeOpacityClass native_opacity_class(const FixedState& state,
    float material_alpha, NativeAlphaEvidence alpha = {}) noexcept;
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
    if (!selector.cache_identity) {
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
};
static_assert(sizeof(NativeShadowConstants)==880, "owned shadow constant layout");

inline bool configured_boolean(const RendererConfig& config,const char* key,bool fallback){const auto* option=config.find(key);return option&&option->type==RendererOptionType::boolean?option->boolean_value:fallback;}
inline int configured_integer(const RendererConfig& config,const char* key,int fallback){const auto* option=config.find(key);return option&&option->type==RendererOptionType::integer?option->integer_value:fallback;}
// The terrain material switches after the terrain_quality tier (terrain_material.h).
inline TerrainMaterialSwitches configured_terrain_material(const RendererConfig& config){
 return terrain_material_switches(configured_integer(config,"terrain_quality",1),
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
void append_native_shader_programs(std::vector<ShaderProgram>& out);
void append_terrain_tessellation_shader_programs(std::vector<ShaderProgram>& out);
void append_terrain_bake_shader_programs(std::vector<ShaderProgram>& out);
void append_impostor_shader_programs(std::vector<ShaderProgram>& out);
void append_hiz_shader_programs(std::vector<ShaderProgram>& out);
void append_liquid_wave_shader_programs(std::vector<ShaderProgram>& out);
void append_ambient_occlusion_shader_programs(std::vector<ShaderProgram>& out);
void append_sky_shader_programs(std::vector<ShaderProgram>& out);
void append_fog_volume_shader_programs(std::vector<ShaderProgram>& out);
void append_ambient_particle_shader_programs(std::vector<ShaderProgram>& out);
void append_wind_trail_shader_programs(std::vector<ShaderProgram>& out);
void append_ground_cover_shader_programs(std::vector<ShaderProgram>& out);
void append_critter_shader_programs(std::vector<ShaderProgram>& out);
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
std::future<TerrainTileFamilies> load_terrain_tile_families_async(const TerrainCpuAsset& terrain);
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

class Renderer {
    friend struct ContactBlendSmokeAccess;
    friend struct LiquidSurfaceSmokeAccess;
    friend struct SceneCompositeSmokeAccess;
    friend struct AmbientMapRegressionAccess;
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();
    struct NativeQueueCheckpoint {
        std::size_t draws = 0;
        std::size_t dynamic_bytes = 0;
        std::uint64_t epoch = 0;
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
    bool consume_display_apply(DisplaySettings& settings) noexcept { return debug_overlay_.consume_display_apply(settings); }
    void finish_display_apply(const DisplaySettings& settings, HRESULT result, const char* message) noexcept {
        debug_overlay_.finish_display_apply(settings, result, message);
    }
    HRESULT clear(DWORD rectangle_count, const D3DRECT* rectangles,
                  DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
        return clear(0, nullptr, flags, color, z, stencil);
    }
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
    HRESULT draw_frame(const std::vector<LegacyDrawPacket>& packets,
                       const std::vector<std::uint8_t>& vertex_bytes,
                       const std::vector<WORD>& indices);
    HRESULT queue_native(const NativeGeometryView& view, const FixedState& state,
                         SurfaceImage* texture,
                         NativeGeometryResult* result = nullptr);
#ifdef EID3D11_EMBEDDED
    HRESULT queue_embedded_records(const NativePreparedSubmission& submission,
        const FixedState& state, std::uint64_t primary,
        const OwnedVisualSurface* secondary, NativeGeometryResult* result);
    HRESULT queue_embedded_figure(const NativeGeometryView& view, const FixedState& state,
        ID3D11ShaderResourceView* primary, ID3D11ShaderResourceView* secondary,
        const NativeWorldLighting* lighting, const std::vector<RetainedWorldPointLight>& lights);
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
    bool queue_native_static_shadow_depth(const RetainedShadowCasterValue* casters,
        std::uint32_t count,std::vector<RetainedShadowCasterValue>& missing) noexcept;
    HRESULT queue_native_records(const NativePreparedSubmission& submission,
                                 const FixedState& frame_state,SurfaceImage* wound,
                                 NativeGeometryResult* result = nullptr);
    HRESULT flush_native(unsigned layout_slot = 0);
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
    const std::array<float,16>* native_frozen_cull_clip() const noexcept { return native_cull_frozen_?&native_cull_clip_:nullptr; }
    bool set_native_world_lighting(const NativeWorldLighting& lighting) noexcept;
    // world_time: the game's world clock of these lights (emissive-light pulse).
    bool set_native_point_lights(const std::vector<RetainedWorldPointLight>& lights,double world_time=0) noexcept;
    bool native_shadows_enabled() const noexcept { return native_shadows_enabled_; }
    bool native_point_shadows_enabled() const noexcept { return native_point_shadows_enabled_; }
    bool begin_native_shadows(const NativeShadowFrame& frame, std::uint8_t& dirty_mask) noexcept;
    void native_shadow_cull_projection(NativeShadowCullProjection& projection) const noexcept;
    bool begin_native_point_shadow_faces(UINT slot,UINT light_index,const NativePointShadowFrame& faces) noexcept;
    bool end_native_point_shadow_faces(bool commit) noexcept;
    bool begin_native_point_shadow_batch(std::uint64_t& generation,bool* pressure=nullptr) noexcept;
    bool allocate_native_point_shadow(bool overlay,int& slot) noexcept;
    void release_native_point_shadow(std::uint64_t generation,int slot) noexcept;
    void publish_native_point_shadows(const std::vector<NativePointShadowPublication>& lights,
        const NativePointShadowStats& stats) noexcept;
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
    HRESULT present_image(const SurfaceImage& source, const RECT* source_rect, const DDCOLORKEY* source_key);
    // A large picture reached the primary this frame (loading screens, movies,
    // also when not exactly full-screen): the load reveal keeps the latest one.
    void mark_load_picture() noexcept { load_picture_this_frame_ = true; }
    void retire_surface(std::uint64_t surface_id) noexcept;
    void retire_camp_texture(std::uint64_t surface_id,
                             std::uint64_t generation) noexcept;
    HRESULT queue_character_pane_text(
        const CharacterPaneTextFrame& frame, std::uint64_t ui_epoch,
        std::uint64_t frame_serial, const RECT& destination,
        const RECT* source_rect);
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
    // Returns a released GPU-primary surface's exact pixels to the CPU by a
    // one-shot staging readback. A surface restored twice within
    // kSurfaceRepeatReadFrames stays CPU-resident (cpu_pinned) so a hot reader
    // never costs a readback per frame; pin_now does the same at once.
    HRESULT restore_surface_pixels(SurfaceImage& image, bool pin_now = false);
    static constexpr std::uint64_t kSurfaceRepeatReadFrames = 120;
    static constexpr std::uint8_t kSurfaceCpuGraceFrames = 2;
    // Drops a draw-only GPU-primary generation after its sampling grace period.
    void release_surface_pixels(SurfaceImage& image) noexcept;
    void sampled_surface_pixels(SurfaceImage& image,bool resolved=false) noexcept;
    // Completed proxy sources carry no pixels. The proxy resolves a current
    // surface generation to its texture, uploading it if needed.
    using SurfaceTextureResolver = HRESULT (*)(void* context, ID3D11Device* device,
        std::uint64_t id, std::uint64_t generation, ComPtr<ID3D11Texture2D>& texture);
    void set_surface_texture_resolver(SurfaceTextureResolver resolver, void* context) noexcept {
        surface_texture_resolver_ = resolver; surface_texture_resolver_context_ = context;
    }
    HRESULT composite_world();
    void queue_camp_decoration(const CampDecoration& frame, bool include_popup = true) noexcept;
    HRESULT begin_camp_popup(bool clear);
    void end_camp_popup(bool complete) noexcept;
    void invalidate_camp_popup() noexcept { camp_popup_ready_=false; }
    void reset_camp_popup() noexcept;
    HRESULT flush_ui();
    bool has_pending_ui() const noexcept { return !pending_blits_.empty() || cursor_pending_; }
    HRESULT present();
    void release();
    // Direct embedded-host seam; active targets are acquired at each call,
    // including the scene-to-interface boundary and render-scale changes.
    HRESULT visit_embedded_device(void* user, EmbeddedDeviceVisitor visitor) const {
        return owned::visit_embedded_device(device_.Get(), context_.Get(),
            rtv_.Get(), dsv_.Get(), user, visitor);
    }
    HRESULT visit_embedded_output(void* user, EmbeddedOutputVisitor visitor) const {
        return owned::visit_embedded_output(device_.Get(), context_.Get(),
            backbuffer_rtv_.Get(), user, visitor);
    }
    bool ready() const { return device_ != nullptr; }
    ID3D11Device* resource_device() const noexcept { return device_.Get(); }
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
    bool test_native_morph_span(const ResourceIdentity& identity,
                                UINT& page, UINT& offset, UINT& bytes,
                                UINT& owners, UINT& references) const noexcept;
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
    };
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
    struct PendingBlit { std::uint64_t source_id{}, cache_key{}, generation{}, base_generation{}, revision{}, ui_epoch{}, frame_serial{}; bool native_canvas{true}; bool keyed{}, gpu{}, cached{}, partial{}, force_opaque_alpha{}, batchable_text{}, source_is_surface{}, source_destroyed{}, quarter_turn{}, backdrop{}, presentation_fill{}; widescreen::HudComposition composition{}; RECT source{}, destination{}, update{}; DDCOLORKEY color_key{}; UINT width{}, height{}, pitch{}; std::uint32_t diffuse{0xffffffffu}; float u0{}, v0{}, u1{1}, v1{1}; std::vector<std::uint32_t> pixels; std::shared_ptr<const CharacterPaneGlyphAsset> text_asset; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv; };
    struct OverlayTexture { UINT width{}, height{}; std::uint64_t generation{}, revision{}, surface_id{}; RECT source{}; bool keyed{}; DDCOLORKEY color_key{}; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv; };
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
        UINT used = 0;
        std::uint64_t last_used = 0;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        std::unordered_map<std::uint64_t, NativeTextureArraySlot> slots;
    };
    struct NativeMeshKey {
        NativeMeshCacheSelector selector{};
        std::string resource_key;
        std::uint32_t world_part = 0;
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
                world_part == other.world_part &&
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
    };
    static_assert(sizeof(NativeInstanceGpu) == 352,
                  "NativeInstanceGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativeWorldLighting) == 48,
                  "NativeWorldLighting must match the HLSL constant-buffer layout");
    static_assert(sizeof(NativeDrawGpu) == 100,
                  "NativeDrawGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativeDrawWorkGpu) == 36,
                  "NativeDrawWorkGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativePreparedVertexGpu) == 64,
                  "NativePreparedVertexGpu must match the HLSL structured-buffer layout");
    static_assert(sizeof(NativeWorldVertexGpu) == 64,
                  "NativeWorldVertexGpu must match the HLSL structured-buffer layout");
    struct NativeMaterialKey {
        // Only state consumed by the current native D3D11 pipeline belongs in
        // this key. Legacy object identity and dormant D3D7 slots must not
        // fragment otherwise identical GPU work.
        std::array<std::uint32_t, 30> state{};
        std::uint32_t shadow = 0, contact_map = 0;
        std::uint64_t texture_backing = 0;
        std::uint64_t texture_generation = 0;
        bool operator==(const NativeMaterialKey& other) const noexcept {
            return state == other.state && shadow == other.shadow && contact_map == other.contact_map && texture_backing == other.texture_backing &&
                texture_generation == other.texture_generation;
        }
    };
    struct NativeMaterialKeyHash {
        std::size_t operator()(const NativeMaterialKey& key) const noexcept;
    };
    struct NativePreparedMeshSlot {
        struct DeformationKey {
            std::array<float, 24> values{};
            std::string morph_resource;
            UINT first_frame = 0, second_frame = 0, wave_flags = 0;
            float morph_fraction = 0;
            UINT pose_slot = UINT_MAX;
            UINT pose_allocation = UINT_MAX;
            std::uint64_t layer_shift_revision = 0; // terrain: shader-applied layer shifts
            bool operator==(const DeformationKey& other) const noexcept {
                return values == other.values && morph_resource == other.morph_resource &&
                    layer_shift_revision == other.layer_shift_revision &&
                    first_frame == other.first_frame && second_frame == other.second_frame &&
                    wave_flags == other.wave_flags && morph_fraction == other.morph_fraction &&
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
    struct NativeMorphFrames {
        NativeGeometrySpan allocation{};
        UINT frame_count = 0, point_count = 0;
        UINT references = 0, owners = 0;
        std::uint64_t generation = 1;
        std::uint64_t last_used_frame = 0;
        bool occupied = false;
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
        UINT preparations = 0,components = 0; // test seam; production never increments these
    };
    static_assert(sizeof(NativePoseGpu)==120 && sizeof(NativePoseNodeGpu)==104 && sizeof(NativeWorldStateGpu)==24,
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
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        DWORD source_fourcc = 0;
        UINT width = 0;
        UINT height = 0;
        UINT mip_count = 0;
        std::uint64_t last_used = 0;
        std::uint64_t shared_floor = 0; // Last neighbour load that counted this reuse.
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
    };
    struct NativeQueuedDraw {
        NativeMesh* mesh = nullptr;
        UINT geometry_page = UINT_MAX;
        UINT morph_page = UINT_MAX;
        UINT morph_allocation = UINT_MAX;
        std::uint64_t morph_generation = 0;
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
    };
    struct NativeInstanceRecord {
        NativeQueuedDraw command{};
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
    };
    struct NativeInstanceUpdate {
        UINT destination = 0, count = 0, flags = 0;
        NativeInstanceGpu value{};
        NativeDrawGpu descriptor{};
    };
    static_assert(sizeof(NativeInstanceUpdate)==464,"NativeInstanceUpdate must match the structured HLSL layout");
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
        UINT geometry_page = UINT_MAX, morph_page = UINT_MAX;
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
            std::uint64_t set_revision = 0, content_revision = 0;
            ComPtr<ID3D11Buffer> work;
            ComPtr<ID3D11ShaderResourceView> work_srv;
            UINT work_capacity = 0;
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
    HRESULT create_pipeline();
    // Creates the shader object of every program the current settings use and
    // this process has not prepared yet, on worker threads (from the embedded
    // bytecode; a missing program is compiled), presenting a progress window
    // while that takes more than 150 ms. Called before the pipelines are
    // created and after every present (settings may have changed).
    void prepare_shaders() noexcept;
    void register_embedded_shaders() noexcept;
    HRESULT load_camp_art();
    HRESULT create_clear_pipeline();
    HRESULT draw_screen_rectangle(const ScreenRectangleRequest& request,
                                  const D3DVIEWPORT7& viewport,float grid_step=0.f);
    HRESULT clear_rectangles(DWORD rectangle_count, const D3DRECT* rectangles,
                             DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT create_native_pipeline();
    HRESULT render_pending_blits();
    HRESULT draw_hud_asset_frames();
    std::shared_ptr<const TextureCpuAsset> hud_frame_texture_;
    bool hud_frame_texture_failed_=false;
    std::uint64_t hud_frames_frame_=UINT64_MAX;
    HRESULT get_native_texture_array_slice(SurfaceImage& image,
                                           ComPtr<ID3D11ShaderResourceView>& srv,
                                           UINT& slice,
                                           const TerrainTexturePage* authored_page=nullptr);
    HRESULT get_native_owned_texture(
        const NativeOwnedTextureView& source,
        ComPtr<ID3D11ShaderResourceView>& srv, std::uint64_t& backing, NativeGeometryResult* result, bool world_mips = false);
    HRESULT get_native_owned_texture_array(ID3D11ShaderResourceView* source,
        std::uint64_t backing, ComPtr<ID3D11ShaderResourceView>& array, UINT& slice, NativeGeometryResult* result);
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
        UINT& state_bucket);
    HRESULT get_native_material_group(const NativeMaterialKey& key,const FixedState& state,
        ID3D11ShaderResourceView* texture,bool texture_array,SemanticKind semantic,
        NativeOpacityClass opacity,std::uint64_t state_key,UINT& group);
    void bind_native_material_group(NativeInstanceRecord& record,UINT group) noexcept;
    void trim_native_material_groups() noexcept;
    void order_native_groups(bool shadows);
    HRESULT queue_native_impl(const NativeGeometryView& view,
                              const FixedState& state, SurfaceImage* texture,
                              NativeGeometryResult* result, bool prepared,
                              ID3D11ShaderResourceView* direct_texture = nullptr,
                              std::uint64_t direct_texture_identity = 0,
                              std::uint64_t direct_texture_generation = 0,
                              bool direct_array = false, UINT direct_slice = 0,
                              UINT* registered = nullptr);
    HRESULT queue_native_owned_impl(const NativeGeometryView& view,const FixedState& state,
                                    NativeGeometryResult* result,UINT* registered);
    // Invalidates every active slot of the meshes marked retiring in one slot
    // pass, then erases those meshes.
    void erase_retiring_native_meshes() noexcept;
    void invalidate_native_prepared_handles() noexcept;
    HRESULT get_native_morph_frames(const NativeMorphFramesView& source,
                                    UINT& first, UINT& second,
                                    UINT& page, UINT& allocation,
                                    std::uint64_t& generation);
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
    HRESULT create_shadow_history_targets();
    bool prepare_shadow_history() noexcept;
    void commit_shadow_history() noexcept;
    HRESULT grow_native_point_shadow_page(std::size_t page,UINT cubes,bool overlay);
    std::uint64_t native_point_shadow_bytes() const noexcept;
    void point_shadow_capacity_failure(const char* reason,HRESULT hr) noexcept;
    HRESULT flush_native_shadows();
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
    bool native_morph_allocation_current(UINT allocation,
                                         std::uint64_t generation) const noexcept;
    void acquire_native_morph_owner(const NativeQueuedDraw& draw) noexcept;
    void release_native_morph_owner(const NativeQueuedDraw& draw) noexcept;
    void refresh_native_mesh_records(NativeMesh& mesh) noexcept;
    HRESULT upload_native_work_range(const NativeDispatchRange& range);
    HRESULT trim_native_mesh_cache();
    // Retires unowned geometry last used before frame `before`.
    HRESULT retire_stale_native_geometry(std::uint64_t before);
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
    enum class TerrainProfilePart : unsigned { live, live_routed, cached, ground, water, foam, stock_water };
    static const char* terrain_profile_scope(bool neighbour, bool tessellated, TerrainProfilePart part) noexcept;
    RendererResourceSnapshot resource_snapshot() const;
    CutoutShadowScopeSnapshot cutout_shadow_scope_snapshot() const noexcept;

    // Declared before device resources so implicit destruction releases them
    // before our own NVAPI client and module reference.
    struct NvidiaShaderCacheApi {
        HMODULE module = nullptr;
        int (__cdecl* unload)() = nullptr;
        ~NvidiaShaderCacheApi();
        void reset() noexcept;
    } nvidia_shader_cache_api_;
    HRESULT disable_nvidia_shader_disk_cache() noexcept;

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
    UINT target_width() const noexcept { return scene_composited_ ? output_width_ : width_; }
    UINT target_height() const noexcept { return scene_composited_ ? output_height_ : height_; }
    void apply_render_scale();
    ComPtr<IDXGISwapChain> swap_;
    UINT swap_flags_=0;bool tearing_supported_=false;
    FrameLimiter frame_limiter_;  // player frame_limit option, paces Present
    std::uint32_t shader_groups_prepared_ = 0;  // prepare_shaders groups already created
    std::uint32_t shader_on_demand_reported_ = 0;  // shader_cache on-demand compiles already logged
    HRESULT create_swap_chain();
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIAdapter3> adapter3_;
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
    ComPtr<ID3D11Texture2D> interface_depth_;
    ComPtr<ID3D11DepthStencilView> interface_dsv_;
    // One colour snapshot at the first liquid batch, owned through target resize.
    struct WaterReflectionConstants {
        std::array<float,16> clip{},inverse{};
        std::array<float,4> target{};
        std::array<float,4> quality{}; // SSR march steps (water_reflection_quality), history mip, history valid, reserved
        std::array<float,16> history_clip{}; // world-to-clip of the previous frame's colour (slice 1)
        std::array<float,16> history_inverse{}; // its inverse, for the previous frame's particle depth
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
    void draw_game_particle_depth(UINT vertex_count);
    ComPtr<ID3D11Texture2D> water_scene_color_;
    ComPtr<ID3D11ShaderResourceView> water_scene_srv_;
    ComPtr<ID3D11Texture2D> water_scene_depth_;ComPtr<ID3D11ShaderResourceView> water_scene_depth_srv_;
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
    HRESULT ensure_ground_cover_pipeline() noexcept;
    HRESULT update_ground_cover_maps(const AmbientWorldSource& inputs) noexcept;
    void release_ground_cover() noexcept;
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
    // Terrain material cache (terrain_bake.h; renderer_terrain_bake.cpp).
    struct TerrainBakeFrame{bool bake=false,sample=false;}; // this flush bakes pages / draws from the cache
    struct TerrainBakeResidency; // pages, slots and wanted set (renderer_terrain_bake.cpp)
    HRESULT ensure_terrain_bake_pipeline() noexcept;HRESULT ensure_terrain_bake_programs() noexcept;
    HRESULT create_terrain_bake_atlases(double near_bytes_wanted,double far_bytes_wanted) noexcept;
    void update_terrain_bake_wanted() noexcept;
    void select_terrain_bake_pages() noexcept;
    void upload_terrain_bake_tables() noexcept;
    void finalize_terrain_bake() noexcept;
    void release_terrain_bake() noexcept;
    TerrainBakeFrame update_terrain_bake() noexcept;
    void draw_terrain_bake(ID3D11Buffer* arguments,UINT offset,bool tessellated,UINT map) noexcept;
    void write_terrain_bake_draw(UINT flag,UINT map) noexcept;
    void refresh_terrain_bake_slices() noexcept;
    HRESULT ensure_terrain_bake_tables() noexcept;
    void finish_terrain_bake() noexcept;
    bool draw_terrain_baked(ID3D11Buffer* arguments,UINT offset,bool tessellated,bool route,bool snow,UINT map,bool single) noexcept; // true: routed
    void report_terrain_bake_work() noexcept;
    bool terrain_bake_touches(UINT map,const float lo[2],const float hi[2]) const noexcept;
    void begin_terrain_bake_span(std::size_t& span) noexcept;
    void end_terrain_bake_span(std::size_t span) noexcept;
    void update_terrain_bake_budget() noexcept;
    std::shared_ptr<TerrainBakeResidency> terrain_bake_state_;
    // Per-map cell tables, built on a worker at map load (start_terrain_bake_cells).
    void start_terrain_bake_cells() noexcept;
    std::future<std::shared_ptr<const TerrainBakeCells>> terrain_bake_cells_future_;
    std::shared_ptr<const TerrainBakeCells> terrain_bake_cells_;std::uint64_t terrain_bake_cells_generation_=0;
    ComPtr<ID3D11VertexShader> terrain_bake_vs_,terrain_bake_tess_vs_;ComPtr<ID3D11HullShader> terrain_bake_hs_;ComPtr<ID3D11DomainShader> terrain_bake_ds_;
    ComPtr<ID3D11PixelShader> terrain_bake_ps_;ComPtr<ID3D11RasterizerState> terrain_bake_raster_;
    // Routing: the baked variant's vertex/hull stages and lean pixel variants (plain, snow trails).
    ComPtr<ID3D11VertexShader> terrain_baked_vs_;ComPtr<ID3D11HullShader> terrain_baked_hs_;ComPtr<ID3D11PixelShader> terrain_baked_ps_,terrain_baked_snow_ps_;
    ComPtr<ID3D11Buffer> terrain_bake_values_,terrain_bake_draw_; // b9 values; b13 per draw (corner / routed flag, map slice)
    // Staging atlas (RGBA8 with mips, the bake's target) and its BC1 / BC7 block targets.
    ComPtr<ID3D11Texture2D> terrain_bake_texture_;ComPtr<ID3D11RenderTargetView> terrain_bake_rtv_;ComPtr<ID3D11ShaderResourceView> terrain_bake_srv_;
    ComPtr<ID3D11Texture2D> terrain_bake_blocks_,terrain_bake_near_blocks_;
    ComPtr<ID3D11UnorderedAccessView> terrain_bake_block_uavs_[terrain_bake_mips],terrain_bake_near_block_uavs_[terrain_bake_mips];
    // Near (BC7 or RGBA8, t126) and far (BC1, t124) atlases; page tables (t125 drawn, t123 baking).
    ComPtr<ID3D11Texture2D> terrain_bake_near_,terrain_bake_far_;ComPtr<ID3D11ShaderResourceView> terrain_bake_near_srv_,terrain_bake_far_srv_;
    ComPtr<ID3D11Buffer> terrain_bake_pages_,terrain_bake_bake_pages_;ComPtr<ID3D11ShaderResourceView> terrain_bake_pages_srv_,terrain_bake_bake_pages_srv_;
    ComPtr<ID3D11SamplerState> terrain_bake_sampler_;
    ComPtr<ID3D11ComputeShader> terrain_bake_bc1_cs_,terrain_bake_bc7_cs_;ComPtr<ID3D11Buffer> terrain_bake_bc1_constants_;
    TerrainBakeValues terrain_bake_values_cpu_{};
    std::uint64_t terrain_bake_map_=0;
    int terrain_bake_mode_=0,terrain_bake_density_=0,terrain_bake_reach_=0,terrain_bake_budget_=0,terrain_bake_stretch_=0,terrain_bake_bc1_=0,terrain_bake_frame_texels_=0;
    bool terrain_bake_near_bc7_=false,terrain_bake_pipeline_failed_=false,terrain_bake_failed_=false,terrain_bake_bound_=false,terrain_bake_drawn_=false;
    // Pixel shader invocations of the first bake frame (pipeline statistics around each bake draw), logged once.
    std::vector<ComPtr<ID3D11Query>> terrain_bake_queries_;bool terrain_bake_measure_=false,terrain_bake_measured_=false;
    // Ground cover placement, every frame: a cell pass (each window cell once)
    // feeds per-class jobs in rounds through three stages per class (cells,
    // habitat, build; renderer_ground_cover.cpp kPlaceClasses), each class
    // compiled with only its rules' features; neighbours run all rules in one
    // set of three stages.
    static constexpr unsigned kGcPlaceClasses = 6, kGcPixelKinds = 4;
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
    std::vector<std::uint64_t> gc_cell_masks_;std::uint64_t gc_cell_masks_key_=UINT64_MAX;ComPtr<ID3D11ShaderResourceView> gc_cell_mask_srv_;
    HRESULT gc_ensure_stage_buffers(UINT capacity);
    HRESULT gc_ensure_cell_jobs(UINT cells);
    // Active map per tile: area class byte | base type << 8 | tree channels (r g b a nonzero) << 16.
    std::vector<std::uint32_t> gc_tile_classes_;std::uint64_t gc_tile_classes_key_=UINT64_MAX;
    std::vector<std::uint8_t> gc_tree_mask_;              // active map per tile: tree channels nonzero
    std::uint64_t gc_tree_epoch_=0;                      // counts rebuilds of gc_tree_mask_
    bool fog_cloud_visible_=false;                       // this frame's fog constants carry a cloud shadow map (bind_world_fog)
    ComPtr<ID3D11Buffer> gc_work_;ComPtr<ID3D11UnorderedAccessView> gc_work_uav_;ComPtr<ID3D11ShaderResourceView> gc_work_srv_; // placed-instance work per pass
    ComPtr<ID3D11ComputeShader> gc_finalize_cs_,gc_paint_cs_,gc_fields_cs_;
    // Settings permutations: shade [far maps].
    std::array<ComPtr<ID3D11ComputeShader>,2> gc_shade_cs_;
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
    std::array<ComPtr<ID3D11VertexShader>,4> gc_vs_;std::array<ComPtr<ID3D11PixelShader>,48> gc_ps_;
    std::array<ComPtr<ID3D11ComputeShader>,8> gc_prep_cs_;
    // Depth prepass permutations (cover, snow pile): the material programs' placement, alpha test only.
    std::array<ComPtr<ID3D11VertexShader>,3> gc_depth_vs_;std::array<ComPtr<ID3D11PixelShader>,2> gc_depth_ps_;
    ComPtr<ID3D11Buffer> gc_draw_inputs_;ComPtr<ID3D11ShaderResourceView> gc_draw_input_srv_;ComPtr<ID3D11UnorderedAccessView> gc_draw_input_uav_;
    std::array<std::uint8_t,kGroundCoverMeshes> gc_mesh_kind_{};
    ComPtr<ID3D11VertexShader> gc_trail_vs_,gc_trail_full_vs_,gc_object_vs_;ComPtr<ID3D11PixelShader> gc_trail_ps_,gc_trail_decay_ps_,gc_object_ps_;
    ComPtr<ID3D11Buffer> gc_frame_constants_,gc_paint_constants_;
    std::array<ComPtr<ID3D11Buffer>,kGroundCoverMeshes> gc_draw_constants_; // per mesh pool base; rebuilt with the pool
    ComPtr<ID3D11Buffer> gc_vertices_,gc_indices_,gc_rules_,gc_args_,gc_counts_,gc_instances_;
    ComPtr<ID3D11ShaderResourceView> gc_vertex_srv_,gc_rule_srv_,gc_instance_srv_,gc_fields_srv_,gc_color_srv_,gc_tiles_srv_;
    ComPtr<ID3D11ShaderResourceView> gc_bark_srv_,gc_stone_srv_,gc_trees_srv_;
    ComPtr<ID3D11Buffer> gc_zone_pieces_;
    std::vector<std::uint8_t> gc_zone_uploaded_values_;
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
    std::array<UINT,kGroundCoverMeshes> gc_mesh_first_index_{},gc_mesh_index_count_{},gc_capacity_{},gc_base_{};
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
    struct SnowTrack{std::uint64_t id=0;float x=0,y=0;std::uint64_t frame=0;float along=0;}; // along: path metres walked
    std::vector<SnowTrack> gc_snow_tracks_;           // each walking unit's previous position, by lifecycle id
    std::vector<GroundUnitTrail> gc_unit_trails_;     // trail key and body size of gc_unit_input_ rows
    std::vector<FootprintTrailPoint> gc_snow_prints_; // visible prints of walking makers (snow trails of unseen units)
    std::vector<snow_trails::Segment> gc_snow_segment_input_;
    ComPtr<ID3D11ShaderResourceView> snow_terrain_srv_;float snow_terrain_window_[4]{};std::uint32_t snow_terrain_tiles_[4]{};
    snow_trails::ActiveTiles gc_snow_tiles_;          // per 8 m tile: active until (game seconds)
    std::array<std::uint32_t,4> gc_snow_active_{};    // active tile bits of gc_snow_[gc_snow_index_]
    unsigned gc_snow_full_writes_=0;                  // frames left that write every tile (after a move or restart)
    // GPU-only per-map contact art; CPU tile payloads are temporary uploads.
    struct ContactBlendMaps {
        ComPtr<ID3D11ShaderResourceView> tiles,atlas;
        ComPtr<ID3D11Texture2D> texture;
        std::uint64_t ready=0,pending=0;
        UINT width=0,height=0,mips=0;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    };
    ContactBlendMaps contact_maps_;
    HRESULT build_contact_tiles(const TerrainCpuAsset& terrain,UINT tx,UINT ty,ContactBlendMaps& maps,
        std::vector<std::array<std::uint32_t,4>>* rows=nullptr);
    HRESULT copy_contact_atlas(ID3D11Texture2D* source,UINT slice,UINT material,ContactBlendMaps& maps);
    ComPtr<ID3D11SamplerState> contact_sampler_;
    std::uint64_t contact_map_generation_=0,contact_constants_generation_=UINT64_MAX,contact_retry_frame_=0,contact_update_frame_=UINT64_MAX;
    bool contact_incompatible_logged_=false;
    // Terrain material blend (terrain_tile_blend.h): the map's art rows kept
    // for the grid's second half and whether the blend rows are in.
    std::vector<std::array<std::uint32_t,4>> contact_tile_rows_;UINT contact_tiles_x_=0,contact_tiles_y_=0;
    // ready: this map's families were tried; rows: the grid holds blend rows.
    bool tile_blend_ready_=false,tile_blend_rows_=false;float tile_blend_margin_=0;
    HRESULT apply_terrain_tile_blend(const TerrainTileFamilies& families) noexcept;
    // The active map's tile families, loaded off the render thread (one job at
    // a time) for ambient_map_generation_ while the material blend or ground
    // cover is on. poll_terrain_tile_families: true once tile_families_ is in.
    std::future<TerrainTileFamilies> tile_families_job_;
    // Started as soon as a new main terrain source is published (inside the
    // game's load), adopted by the first poll for that same terrain.
    std::future<TerrainTileFamilies> tile_families_prestart_;
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
        std::future<TerrainTileFamilies> families;
        std::vector<std::uint16_t> base_slots;std::vector<std::uint32_t> slot_types;std::vector<std::uint8_t> base_lanes;
        // Terrain material blend of this neighbour's own terrain (its own
        // families and atlas; terrain_tile_blend.h): until its families load,
        // the contact grid's rows and the family-independent blend inputs.
        // contact.tiles always has the blend half (rows [ty,2ty), no material
        // until the families load), as the active map's grid does.
        std::vector<std::array<std::uint32_t,4>> contact_rows;
        std::shared_ptr<TerrainTileBlendInput> blend_input;
        // The terrain material cache's cell tables of this neighbour (map-local
        // 8 m cells), built by its stream worker after the weld.
        std::shared_ptr<const TerrainBakeCells> bake_cells;
    };
    std::vector<GcNeighbour> gc_neighbours_;
    void add_ground_cover_neighbour(const NeighbourMapUpload& upload,const std::array<float,16>& active_to_world,UINT contact_map) noexcept;
    // Once a neighbour's tile families load: its beach-sand cover types and
    // its terrain material blend rows.
    void apply_neighbour_tile_families(GcNeighbour& neighbour) noexcept;
    HRESULT prepare_ground_cover_neighbour(GcNeighbour& neighbour) noexcept;
    bool terrain_full_sun_=false; // terrain lighting compares the full sun (set with the terrain frame)
    std::future<std::vector<ComPtr<ID3DBlob>>> gc_compile_;   // worker shader compile
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
    ComPtr<ID3D11SamplerState> water_reflection_sampler_;
    std::array<float,16> water_capture_clip_{},water_capture_inverse_{};
    std::uint64_t water_capture_frame_ = UINT64_MAX;
    bool water_capture_ready_ = false, water_failure_reported_ = false;
    bool water_late_draw_reported_ = false; // opaque world draw after the water copy (probe event, once)
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
    void clear_water_reflection_bindings() noexcept;
    HRESULT ensure_liquid_normal_map();
    // The map swamp frame's texture (TerrainCpuAsset::swamp_frame_texels),
    // rebuilt per ambient map generation and bound at t33 for liquid draws.
    void bind_liquid_swamp_frame() noexcept;
    // The generated liquid maps, made on a worker from device creation on.
    struct LiquidMapsPrepared {HRESULT hr=E_FAIL;ComPtr<ID3D11ShaderResourceView> normal,detail;std::vector<std::array<float,2>> lava_samples;};
    std::future<LiquidMapsPrepared> liquid_maps_job_;
    void start_liquid_maps_job() noexcept;
    std::uint64_t water_liquid_batches_=0,water_reflection_batches_=0,water_reflection_resource_failures_=0; // temporary SSR witness
    ComPtr<ID3D11ShaderResourceView> liquid_normal_srv_,liquid_detail_srv_;ComPtr<ID3D11SamplerState> liquid_normal_sampler_;
    ComPtr<ID3D11ShaderResourceView> liquid_swamp_frame_srv_;std::uint64_t liquid_swamp_frame_generation_=UINT64_MAX;
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
    void release_world_fog_volume() noexcept;
    ComPtr<ID3D11Buffer> fog_frame_constants_;
    ComPtr<ID3D11SamplerState> fog_sampler_;
    ComPtr<ID3D11ComputeShader> fog_inject_cs_,fog_integrate_cs_;
    ComPtr<ID3D11Texture3D> fog_scatter_[2],fog_integrated_;
    ComPtr<ID3D11UnorderedAccessView> fog_scatter_uav_[2],fog_integrated_uav_;
    ComPtr<ID3D11ShaderResourceView> fog_scatter_srv_[2],fog_integrated_srv_,fog_mist_srv_;
    std::array<float,4> fog_mist_grid_{};
    std::array<float,16> fog_previous_clip_{};
    std::uint64_t fog_prepared_frame_=UINT64_MAX,fog_mist_generation_=0;
    double fog_wetness_hours_=-1.0;
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
    ComPtr<ID3D11ComputeShader> sky_shape_cs_,sky_detail_cs_,sky_weather_cs_,sky_lut_cs_,sky_shadow_cs_,sky_clouds_cs_,sky_resolve_cs_,sky_probe_cs_;
    ComPtr<ID3D11VertexShader> sky_vs_;ComPtr<ID3D11PixelShader> sky_ps_;
    ComPtr<ID3D11Buffer> sky_constants_;
    ComPtr<ID3D11SamplerState> sky_wrap_sampler_,sky_clamp_sampler_;
    ComPtr<ID3D11DepthStencilState> sky_depth_state_;ComPtr<ID3D11BlendState> sky_blend_state_;
    ComPtr<ID3D11RasterizerState> sky_raster_state_;
    ComPtr<ID3D11ShaderResourceView> sky_shape_srv_,sky_detail_srv_,sky_weather_srv_,sky_lut_srv_,sky_shadow_srv_;
    ComPtr<ID3D11UnorderedAccessView> sky_lut_uav_,sky_shadow_uav_;
ComPtr<ID3D11ShaderResourceView> sky_march_srv_;ComPtr<ID3D11UnorderedAccessView> sky_march_uav_;
    ComPtr<ID3D11ShaderResourceView> sky_cloud_srv_[2],sky_probe_srv_[2];
    ComPtr<ID3D11UnorderedAccessView> sky_cloud_uav_[2],sky_probe_uav_[2];
    std::array<float,16> sky_previous_clip_{};
    double sky_wind_offset_[3]{};
    double sky_time_=-1.0;
    float sky_fair_coverage_=-1.f;
    float sky_closure_=-1.f;  // weather closure ramped over game time (sky_step_closure)
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
    char sky_status_[448]="Sky: no world frame yet";
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
    ComPtr<ID3D11DepthStencilState> camp_popup_saved_depth_state_;
    UINT camp_popup_stencil_=0,camp_popup_viewport_count_=0;
    std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> camp_popup_viewports_{};
    UINT camp_popup_width_=0,camp_popup_height_=0;
    bool camp_popup_active_=false,camp_popup_ready_=false;
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
    HRESULT create_prepass_targets();
    HRESULT resolve_world_shadow_mask() noexcept; // the mask pass over the marked pixels (renderer.cpp)
    // Settings-specialised pixel programs (native_shader.h NATIVE_OFF_*,
    // NATIVE_FOG_VOLUME_QUALITY): the key names the features whose uploaded
    // values are 0 (bit 0 anti-tiling, 1 height blend, 2 contact, 3 cliffs,
    // 4 material blend, 5 point shadows, 6 sun shadows, 7 no atmosphere,
    // 8 atmosphere without the fog volume, 9 the cascade coverage debug view
    // (compiled only into that set), 10 soft point shadows off, 11 the fast
    // combiner only, bound per batch). Key 0 draws the general
    // programs (the default preset's set). Another key's set compiles on
    // worker threads into the disk cache once the key holds; the general
    // programs draw until it is ready (renderer.cpp). Every set built this
    // session stays resident, so a key seen before binds in the same frame.
    struct NativeSpecialisationJob;
    using NativeSpecialisedSet=std::array<ComPtr<ID3D11PixelShader>,15>;
    std::shared_ptr<NativeSpecialisationJob> native_spec_job_;
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
    void start_native_specialisation(std::uint32_t key) noexcept;
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
    ComPtr<ID3D11PixelShader> backdrop_ps_;ComPtr<ID3D11SamplerState> backdrop_sampler_;
    ComPtr<ID3D11PixelShader> interface_scaled_ps_;bool interface_scaled_ps_failed_=false;
    bool interface_scaled_ps();
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
    ComPtr<ID3D11ComputeShader> native_pose_cs_;
    ComPtr<ID3D11ComputeShader> native_instance_update_cs_;
    ComPtr<ID3D11Buffer> native_persistent_instances_, native_instance_updates_, native_world_clip_constants_;
    ComPtr<ID3D11ShaderResourceView> native_persistent_instance_srv_, native_instance_update_srv_;
    ComPtr<ID3D11UnorderedAccessView> native_persistent_instance_uav_;
    UINT native_persistent_instance_capacity_ = 0, native_instance_update_capacity_ = 0;
    struct InstanceUploadSlot {
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> srv;
        UINT capacity = 0;
    };
    std::array<InstanceUploadSlot, 8> native_instance_upload_ring_{};
    UINT native_instance_upload_cursor_ = 0;
    std::vector<NativeInstanceRecord> native_instance_records_;
    std::unordered_map<std::uint64_t,NativeStaticShadowRecord> native_static_shadow_records_;
    std::uint64_t native_static_shadow_visit_ = 0;
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
        UINT capacity = 0, count = 0;
        std::unordered_map<std::string, UINT> slices;
        bool failed = false;   // the reason was logged once; crowns draw their triangles
        UINT baked = 0;
        double bake_ms = 0;
    } impostors_;
    HRESULT bake_neighbour_impostors(const NeighbourMapUpload& upload) noexcept;
    HRESULT ensure_impostor_atlas(UINT slices) noexcept;
    void impostor_failure(const char* reason) noexcept;
    std::uint32_t neighbour_impostor_slice(const FigureCpuAsset* figure, std::uint64_t texture) const;
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
    struct NativeTerrainPhaseSlot {
        NativePreparedIdentity identity{};
        std::weak_ptr<const void> owner;
        std::uint64_t revision=0;
        UINT generation=1,allocation=UINT_MAX,count=0;
        bool active=false,valid=false,scheduled=false;
    };
    struct NativeTerrainPhaseWork {
        UINT slot=UINT_MAX,allocation=UINT_MAX,offset=0,count=0;
        std::uint64_t revision=0;
    };
    std::vector<NativeTerrainPhaseSlot> native_terrain_phases_;
    std::vector<NativeTerrainPhaseWork> native_terrain_phase_work_;
    std::vector<NativeDrawWorkGpu> native_terrain_phase_copies_;
    UINT native_pose_input_capacity_ = 0, native_pose_node_capacity_ = 0, native_pose_node_used_ = 0;
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
    ComPtr<ID3D11ComputeShader> native_shadow_prepare_cs_, native_shadow_commit_cs_, native_shadow_build_cs_;
    UINT native_shadow_chunks(const NativeSubmissionLayout& layout,const NativeDispatchRange& range,bool triangles) const noexcept;
    ComPtr<ID3D11VertexShader> native_shadow_vs_;
    ComPtr<ID3D11PixelShader> native_shadow_ps_;
    ComPtr<ID3D11Texture2D> native_shadow_depth_;
    ComPtr<ID3D11ShaderResourceView> native_shadow_depth_srv_;
    std::array<ComPtr<ID3D11DepthStencilView>,5> native_shadow_dsv_;
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
    bool native_shadow_collecting_ = false, native_shadow_failed_ = false;
    bool native_shadows_enabled_ = true, native_shadow_constants_dirty_ = false;
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
    // 0 per-frame color, 1 shadows, 2 persistent static color set.
    std::array<NativeSubmissionLayout,5> native_submission_layouts_;
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
    };
    NativeStaticSet native_static_set_{}, native_terrain_set_{{},{},false,1,3};
    struct NeighbourGpuState {
        std::shared_ptr<const void> owner;
        std::weak_ptr<const void> weak_owner;
        NativeStaticSet opaque{{},{},false,1,4};
        std::vector<NativeSelectedRecord> ordered;
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
    void adopt_prebuilt_pages(const NeighbourMapUpload& upload) noexcept;
    void adopt_prebuilt_textures(const NeighbourMapUpload& upload) noexcept;
    void release_unused_prebuilt(const NeighbourMapUpload& upload) noexcept;
    void retire_dedicated_geometry() noexcept;
    bool dedicated_geometry_stale_ = false; // released neighbours may leave dedicated meshes
    HRESULT render_neighbour_maps(const FixedState& frame,const std::array<float,16>& world_to_clip,bool ordered) noexcept;
    HRESULT adopt_native_static_draws(NativeStaticSet& set,std::uint64_t key,std::size_t first,bool& registered);
    HRESULT flush_native_static_set(NativeStaticSet& set,const FixedState& frame);
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
    std::unordered_map<std::uint64_t, OverlayTexture> overlay_textures_;
    std::unordered_map<NativeTextureArrayKey, std::vector<NativeTextureArrayPage>,
                       NativeTextureArrayKeyHash> native_texture_arrays_;
    std::unordered_map<std::string, NativeOwnedTexture> native_owned_textures_;
    std::uint64_t native_owned_texture_serial_ = 0;
    std::uint64_t native_owned_texture_uploads_ = 0, native_owned_array_uploads_ = 0;
    // While a neighbour map uploads: pool serial at its start (0 = off), and the
    // distinct older textures it reused instead of uploading again.
    std::uint64_t native_texture_share_floor_ = 0, native_texture_shared_ = 0, native_texture_shared_bytes_ = 0;
    std::uint64_t native_prepared_mesh_hits_ = 0;
    std::unordered_map<NativeMeshKey, NativeMesh, NativeMeshKeyHash> native_meshes_;
    std::unordered_map<std::string, UINT> native_morph_frames_;
    std::vector<NativeMorphFrames> native_morph_allocations_;
    std::vector<UINT> native_morph_free_;
    std::vector<NativePreparedMeshSlot> native_prepared_mesh_slots_;
    std::vector<NativePreparedMaterialSlot> native_prepared_material_slots_;
    std::size_t native_prepared_mesh_reuse_cursor_ = 0;
    std::size_t native_prepared_material_reuse_cursor_ = 0;
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
    std::uint64_t native_cache_older_owned_meshes_ = 0;
    std::uint64_t native_released_mesh_slots_ = 0;
    std::uint64_t native_geometry_growths_ = 0;      // general page growths and new pages
    std::uint64_t native_cache_seen_growths_ = 0;
    std::uint64_t native_frame_index_ = 0;
    SurfaceTextureResolver surface_texture_resolver_ = nullptr;
    void* surface_texture_resolver_context_ = nullptr;
    NativeWorldLighting native_world_lighting_{};
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
    std::uint64_t perf_native_morph_bytes_ = 0;
    std::uint64_t perf_native_mesh_hits_ = 0;
    std::uint64_t perf_native_mesh_misses_ = 0;
    std::uint64_t perf_native_mesh_evictions_ = 0;
    // Temporary (camera-motion churn): miss classes and morph-set traffic,
    // reported per present in camera_path lines.
    std::uint64_t perf_native_mesh_content_misses_ = 0;
    std::uint64_t perf_native_mesh_world_misses_ = 0;
    std::uint64_t perf_native_morph_misses_ = 0;
    std::uint64_t perf_native_morph_evictions_ = 0;
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
    HarnessRequest step_test_harness() noexcept;
    bool apply_harness_config(const HarnessCommand& command, std::string& detail) noexcept;
    bool write_harness_screenshot(const std::string& path, std::string& detail) noexcept;
    std::string harness_unready_parts(std::string& ready_detail) noexcept;
    // Load reveal: after a new world epoch the first world frames keep the
    // last loading picture on screen until the map, its neighbours and its
    // ground cover are prepared (or a cap passes), so nothing pops in after
    // the loading screen. Frames keep running (multi-frame preparation needs
    // them); only their presentation is replaced. RENDERER_ARCHITECTURE.md.
    void apply_load_reveal() noexcept;
    const char* load_reveal_waiting_on() noexcept;
    bool load_reveal_holding_ = false;      // also drains neighbours and paints every frame
    bool load_reveal_tracking_ = false;     // timing the current load (hold on or off)
    bool load_picture_this_frame_ = false;  // present_image ran in this frame
    std::uint64_t load_reveal_epoch_ = 0, load_reveal_draws_ = 0, load_reveal_revealed_epoch_ = 0;
    std::int64_t load_reveal_first_world_ = 0;
    unsigned load_reveal_frames_ = 0;
    // Held frames per readiness reason (static strings), for the reveal line.
    std::array<std::pair<const char*, unsigned>, 12> load_reveal_waits_{};
    std::uint64_t load_reveal_va_peak_ = 0;
    ComPtr<ID3D11Texture2D> load_picture_;  // GPU copy of the last loading picture frame
    unsigned harness_stage_ = 0;
    std::uint64_t harness_started_ms_ = 0;
    unsigned harness_wait_ = 0;
    // camera_path (temporary): the previous present's counter, the present
    // count and the last profiler frame written.
    std::int64_t harness_path_previous_=0;unsigned harness_path_present_=0;std::uint64_t harness_path_profile_frame_=0;
    std::array<std::uint64_t,6> harness_path_cache_counters_{};
    std::string harness_capture_directory_;
    std::string harness_overlay_shot_;   // screenshot path waiting for this present's overlay pass
    bool harness_profile_armed_ = false;
};

} // namespace owned
