#pragma once
#include "runtime_diagnostics.h"
#include "frame_waits.h"
#include <map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "attribution_ledger.h"
#include "d3d12_frame.h"

namespace owned {

using Microsoft::WRL::ComPtr;

// A CPU readback of a staging resource that never waits forever: polls
// D3D11_MAP_FLAG_DO_NOT_WAIT for up to timeout_ms (the GPU finishes a copy in
// milliseconds; a GPU that stops completing work otherwise hung the caller).
// On timeout: E_PENDING, one gpu_readback_timeout event naming what and the
// device-removed reason, and reason set for the caller's own failure line.
HRESULT gpu_map_read_bounded(ID3D11DeviceContext* context, ID3D11Device* device, ID3D11Resource* resource,
                             UINT subresource, D3D11_MAPPED_SUBRESOURCE& mapped, const char* what,
                             std::string* reason = nullptr, DWORD timeout_ms = 2000) noexcept;

struct BridgeProfileSnapshot;
struct RendererResourceSnapshot;
struct RetainedSceneIdentitySnapshot;
struct SceneWorldToClipSnapshot;

struct CutoutShadowScopeSnapshot {
    struct Source {
        std::string resource_key;
        std::uint64_t instances = 0, parts = 0;
    };
    bool available = false;
    std::uint64_t presentation_frame = 0;
    std::uint64_t sources = 0, instances = 0, parts = 0;
    std::uint64_t instances_without_resource_key = 0;
    std::uint64_t parts_without_resource_key = 0;
    std::vector<Source> listed_sources;
};

struct CutoutShadowScopeRecord {
    std::string resource_key;
    bool terrain_source = false;
    std::int32_t signed_id = 0;
    std::uint64_t lifecycle_generation = 0, world_epoch = 0;
};

CutoutShadowScopeSnapshot summarize_cutout_shadow_scope(
    std::uint64_t presentation_frame,
    const std::vector<CutoutShadowScopeRecord>& records) noexcept;

std::string format_scene_identity_json(
    const RetainedSceneIdentitySnapshot& identity,
    const SceneWorldToClipSnapshot& camera_context,
    const char* capture_requested_at_utc,
    bool presentation_frame_valid,
    std::uint64_t presentation_frame);

std::string format_retained_static_submission_json(
    const BridgeProfileSnapshot* bridge);

std::string format_terrain_owned_front_end_json(
    const BridgeProfileSnapshot* bridge);

struct PassCounters {
    std::uint64_t draws = 0;
    std::uint64_t triangles = 0;
    std::uint64_t batches = 0;
    std::uint64_t uploads = 0;
    std::uint64_t upload_bytes = 0;
    std::uint64_t capture_cpu_ticks = 0;
    std::uint64_t capture_calls = 0;
    std::uint64_t capture_bytes = 0;
    std::uint64_t packets_created = 0;
    std::uint64_t packets_appended = 0;
};

struct ProfilePassSnapshot {
    std::array<char, 64> name{};
    double cpu_ms = 0.0;
    double gpu_ms = -1.0;
    PassCounters counters{};
};

// GPU sub-scopes split one pass's GPU time between groups of its draws
// (active/neighbour terrain, cached/live, static/dynamic figures...). The
// same-named segments of one pass instance merge into one child. They time
// work between draws, never shader stages inside one draw, and are not added
// to the pass or frame totals (the pass span already contains them).

struct ProfileSubScopeSnapshot {
    std::array<char, 40> name{};
    UINT parent = 0; // index into RenderProfileSnapshot::passes
    UINT segments = 0;
    double gpu_ms = -1.0; // -1: a segment did not resolve
    std::uint64_t draws = 0;
    std::uint64_t triangles = 0;
    std::uint64_t batches = 0;
};

// CPU-only accumulators (RenderProfiler::add_cpu_time): renderer entry points
// the game calls many times per frame between passes (each call would be a
// pass record of its own). Their time can overlap a pass that is open when
// they run, so it is not added to frame_cpu_ms.
constexpr UINT kProfileCpuScopes = 8;
struct ProfileCpuScopeSnapshot {
    std::array<char, 40> name{};
    double cpu_ms = 0.0;
    std::uint64_t calls = 0;
};

struct RenderProfileSnapshot {
    std::uint64_t frame = 0;
    UINT width = 0;
    UINT height = 0;
    bool gpu_valid = false;
    double frame_cpu_ms = 0.0;
    double frame_gpu_ms = 0.0;
    UINT pass_count = 0;
    std::vector<ProfilePassSnapshot> passes; // pass_count entries
    UINT child_count = 0;
    UINT children_dropped = 0; // segments whose timestamp queries could not be created
    std::vector<ProfileSubScopeSnapshot> children; // child_count entries
    UINT cpu_scope_count = 0;
    std::array<ProfileCpuScopeSnapshot, kProfileCpuScopes> cpu_scopes{};
};

// One F11-requested pair only. The color and raw depth images allow a visible
// receiver pixel to be located afterward; these values reproduce its cascade
// selection without changing the live color shader or shadow state.
struct ShadowTemporalSliceSnapshot {
    bool enabled = false, valid = false;
    std::uint64_t caster_revisions = 0;
    std::array<float, 16> world_to_light{}, camera_slice{};
    std::array<float, 4> bias{};
    std::array<float, 3> receiver_minimum{}, receiver_maximum{};
};

struct ShadowTemporalFrameSnapshot {
    std::uint64_t presentation_frame = 0, publication_serial = 0, scene_frame_serial = 0;
    std::uint64_t world_bridge_frame = 0, world_completion_serial = 0;
    bool authored_sun_valid = false, shadows_enabled = false, world_to_clip_valid = false;
    bool world_time_valid = false, clear_full_surface = false;
    double world_time = 0;
    std::int64_t sample_qpc = 0, copy_enqueue_qpc_ticks = 0;
    std::uint32_t clear_flags = 0, clear_rectangles = 0;
    std::array<float, 4> authored_sun_direction{}, authored_sun_diffuse{};
    std::array<float, 16> world_to_clip{};
    std::uint8_t previous_valid_mask = 0, current_enabled_mask = 0;
    std::uint8_t world_to_light_changed_mask = 0, camera_slice_changed_mask = 0;
    std::uint8_t bias_changed_mask = 0, receiver_bounds_changed_mask = 0;
    std::uint8_t validity_changed_mask = 0, dirty_mask = 0, source_dirty_mask = 0;
    std::uint64_t shadow_begin_calls = 0, source_changed_visuals = 0;
    std::uint64_t source_changed_terrain = 0;
    std::array<ShadowTemporalSliceSnapshot, 5> slices{};
};

enum class ShadowTemporalPresentOutcome {
    none, waiting, complete, unavailable, export_failed
};

// The D3D12 backend's GPU timestamps (its executor) in place of the D3D11
// queries: stamp() writes one now (0 when none could be written), value()
// reads it once the GPU finished that work (false while it has not, or when
// it was lost), frequency() is the ticks per second.
struct ProfileTimestamps {
    virtual std::uint64_t stamp() noexcept = 0;
    virtual bool value(std::uint64_t stamp, std::uint64_t& ticks) noexcept = 0;
    virtual std::uint64_t frequency() noexcept = 0;

protected:
    ~ProfileTimestamps() = default;
};

// Low-overhead continuous timings use delayed D3D11 timestamp queries.  The
// optional capture path intentionally stalls while copying each pass result;
// it is a diagnostic action and is never active by default.
class RenderProfiler {
public:
    HRESULT initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    // D3D12 backend (before initialize): passes are timed by these stamps;
    // the D3D11 queries are created but never issued.
    void set_timestamps(ProfileTimestamps* timestamps) noexcept { timestamps_ = timestamps; }
    void shutdown();

    void begin_frame(UINT width, UINT height);
    void begin_pass(const char* name);
    void add_draw(std::uint64_t triangles, std::uint64_t legacy_draws = 1);
    void add_batch(std::uint64_t count = 1);
    void add_upload(std::uint64_t bytes);
    void add_capture(std::uint64_t cpu_ticks, std::uint64_t bytes, bool appended);
    // Native image export runs only for passes selected by the capture policy.
    using CaptureWriter = bool (*)(void*, const std::string&);
    void end_pass(ID3D11Texture2D* capture_source = nullptr,
                  CaptureWriter writer = nullptr, void* writer_context = nullptr, const char* image_extension = "bmp");
    // Opens a named GPU sub-scope inside the active pass, closing the open one
    // unless it has the same name (consecutive same-named groups share one
    // timestamp pair). No-op unless profiling is on; the pass end closes it.
    void sub_scope(const char* name);
    // Draws, triangles and one batch submitted inside the open sub-scope.
    void add_sub_draw(std::uint64_t draws, std::uint64_t triangles);
    // name: a string literal (scopes are matched by address).
    void add_cpu_time(const char* name, std::int64_t qpc_ticks) noexcept;
    void end_frame(UINT present_interval = 0, UINT present_flags = 0);
    void present_return(UINT width, UINT height, UINT present_interval,
                        UINT present_flags, HRESULT result);
    void record_attribution_workload(const BridgeProfileSnapshot* bridge,
                                     const RendererResourceSnapshot& resources) noexcept;
    bool arm_attribution_marker_begin(const AttributionMarkerSelection& selection) noexcept {
        return attribution_.arm_marker_begin(selection);
    }
    bool arm_attribution_marker_end() noexcept { return attribution_.arm_marker_end(); }
    AttributionMarkerStatus attribution_marker_status() const noexcept {
        return attribution_.marker_status();
    }
    bool attribution_enabled() const noexcept { return attribution_.enabled(); }
    void set_enabled(bool enabled) noexcept {
        requested_enabled_ = enabled;
        refresh_enabled();
    }
    void request_capture_next(const BridgeProfileSnapshot* bridge,
                              const RendererResourceSnapshot* resources,
                              const CutoutShadowScopeSnapshot* cutout_scope = nullptr) noexcept;
    bool request_shadow_temporal_pair(ID3D11Texture2D* color, ID3D11Texture2D* depth,
                                      ID3D11Texture2D* shadows,
                                      std::uint64_t first_presentation_frame,
                                      std::uint64_t completed_world_serial) noexcept;
    bool request_shadow_temporal_pair_d3d12(D3D12Frame& frame, D3D12Texture& world,
                                            D3D12Texture& shadows, UINT shadow_side,
                                            std::uint64_t first_presentation_frame,
                                            std::uint64_t completed_world_serial) noexcept;
    bool queue_shadow_temporal_frame_d3d12(D3D12Texture& world, D3D12Texture& shadows,
                                          const ShadowTemporalFrameSnapshot& snapshot) noexcept;
    bool shadow_temporal_armed() const noexcept { return shadow_temporal_armed_; }
    bool shadow_temporal_due(std::uint64_t presentation_frame,
                             std::uint64_t completed_world_serial) const noexcept {
        return shadow_temporal_armed_ &&
            presentation_frame == shadow_temporal_first_frame_ + shadow_temporal_queued_ &&
            completed_world_serial > shadow_temporal_last_world_completion_;
    }
    bool queue_shadow_temporal_frame(ID3D11Texture2D* color, ID3D11Texture2D* depth,
                                     ID3D11Texture2D* shadows,
                                     const ShadowTemporalFrameSnapshot& snapshot) noexcept;
    ShadowTemporalPresentOutcome shadow_temporal_present(
        std::uint64_t presentation_frame) noexcept;

    // Consumes completed timestamp results since the previous report.
    void enable_gpu_journal() noexcept { journal_enabled_ = RuntimeDiagnostics::Enabled(); }
    bool gpu_journal_enabled() const noexcept { return journal_enabled_; }
    std::string take_gpu_pass_report(std::string* frame_waits = nullptr);
    bool frame_active() const { return frame_active_; }
    bool pass_active() const { return pass_active_; }
    bool enabled() const { return enabled_; }
    bool capture_active() const { return capture_active_; }
    const std::string& capture_directory() const noexcept { return capture_directory_; }
    bool current_pass_is(const char* name) const;
    const RenderProfileSnapshot* latest_snapshot() const noexcept {
        return has_latest_snapshot_ ? &latest_snapshot_ : nullptr;
    }

private:
    struct JournalPass {
        std::vector<double> ms;
        std::uint64_t draws = 0, triangles = 0;
    };
    bool journal_enabled_ = false;
    std::map<std::string, JournalPass> journal_passes_;
    std::vector<double> journal_totals_;
    frame_waits::Distribution journal_dlssg_, journal_fsr_prepare_;
    std::uint64_t journal_invalid_ = 0;
    static constexpr UINT kBufferedFrames = 6;

    struct PassRecord {
        std::array<char, 64> name{};
        std::int64_t cpu_begin = 0;
        std::int64_t cpu_end = 0;
        PassCounters counters{};
        AttributionCpuPoint attribution_begin{};
        AttributionCpuPoint attribution_end{};
    };
    struct QueryPair {
        ComPtr<ID3D11Query> begin;
        ComPtr<ID3D11Query> end;
        std::uint64_t begin_stamp = 0, end_stamp = 0;  // ProfileTimestamps (D3D12)
    };
    struct SubScopeRecord {
        std::array<char, 40> name{};
        UINT parent = 0;
        std::uint64_t draws = 0, triangles = 0, batches = 0;
    };
    // Parallel to slots_ (same index). Pass and sub-scope storage grows with
    // the frame's demand; a query pair is created the first time a slot needs
    // that many and reused afterwards.
    struct SubScopeSlot {
        std::vector<QueryPair> queries;
        std::vector<SubScopeRecord> records;
        UINT count = 0;
        UINT dropped = 0;
    };
    struct FrameSlot {
        ComPtr<ID3D11Query> disjoint;
        std::uint64_t frame_stamp = 0;  // ProfileTimestamps: the frame's end, for its readiness
        QueryPair device_span{};
        std::vector<QueryPair> queries;
        std::vector<PassRecord> passes;
        std::uint64_t frame = 0;
        UINT pass_count = 0;
        UINT width = 0;
        UINT height = 0;
        bool pending = false;
        bool attribution_present_returned = false;
        struct CpuScope { const char* name = nullptr; std::int64_t ticks = 0; std::uint64_t calls = 0; };
        std::array<CpuScope, kProfileCpuScopes> cpu_scopes{};
        UINT cpu_scope_count = 0;
        AttributionFrameRecord attribution{};
    };

    bool resolve(FrameSlot& slot, bool wait);
    // A query pair's begin or end: the D3D11 query, or a D3D12 stamp.
    void mark(QueryPair& pair, bool end) noexcept;
    bool read(const QueryPair& pair, bool end, UINT64& ticks) noexcept;
    void end_sub_scope();
    void refresh_enabled() noexcept;
    void capture_pass(ID3D11Texture2D* source, const PassRecord& pass, UINT ordinal,
                      CaptureWriter writer, void* writer_context, const char* image_extension);
    bool write_bmp(const std::string& path, ID3D11Texture2D* source,
                   bool source_is_staging = false);
    bool write_shadow_temporal_depth(const std::string& path, ID3D11Texture2D* staging);
    bool write_shadow_temporal_maps(const std::string& path, ID3D11Texture2D* staging);
    bool export_shadow_temporal_pair();
    void clear_shadow_temporal_pair() noexcept;
    std::string make_debug_root() const;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ProfileTimestamps* timestamps_ = nullptr;
    std::array<FrameSlot, kBufferedFrames> slots_{};
    std::array<SubScopeSlot, kBufferedFrames> sub_slots_{};
    FrameSlot* current_ = nullptr;
    SubScopeSlot* current_subs_ = nullptr;
    FrameSlot* present_slot_ = nullptr;
    std::uint64_t next_frame_ = 0;
    UINT current_pass_ = 0;
    bool enabled_ = false;
    bool environment_enabled_ = false;
    bool requested_enabled_ = false;
    bool frame_active_ = false;
    bool pass_active_ = false;
    bool sub_active_ = false;
    bool sub_scopes_enabled_ = true; // EI_RENDERER_PROFILE_SUBSCOPES=0 turns them off
    bool capture_active_ = false;
    bool capture_next_consumed_ = false;
    bool capture_requested_ = false;
    std::uint64_t requested_capture_frame_ = ~std::uint64_t{0};
    std::int64_t qpc_frequency_ = 1;
    std::string debug_root_;
    std::string capture_directory_;
    std::string pending_normal_frame_metadata_;
    struct ShadowTemporalSlot {
        ComPtr<ID3D11Texture2D> color, depth, shadows;
        D3D12TextureRef color12, depth12, shadows12;
        UINT width12 = 0, height12 = 0, shadow_side12 = 0;
        ShadowTemporalFrameSnapshot snapshot{};
    };
    D3D12Frame* shadow_temporal_d3d12_ = nullptr;
    std::array<ShadowTemporalSlot, 2> shadow_temporal_slots_{};
    std::uint64_t shadow_temporal_first_frame_ = 0;
    std::uint64_t shadow_temporal_last_world_completion_ = 0;
    unsigned shadow_temporal_queued_ = 0;
    bool shadow_temporal_armed_ = false;
    RenderProfileSnapshot latest_snapshot_{};
    bool has_latest_snapshot_ = false;
    std::uint32_t attribution_selected_pending_ = 0;
    AttributionLedger attribution_{};
};

} // namespace owned
