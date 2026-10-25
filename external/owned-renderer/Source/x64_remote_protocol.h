#pragma once
// Wire of the remote D3D11 device (EI_RENDERER_X64_REMOTE=1): the x86
// renderer keeps issuing its D3D11 calls on proxy objects
// (x64_remote_device.cpp); every call becomes a record in one shared-memory
// ring that the x64 host (Host/remote_host.cpp) replays on
// the real device. Fixed-width fields only; no pointer crosses except the
// game HWND and x86 destination addresses the host writes readbacks into
// Readbacks come back through shared memory only; the host opens the game
// process just to see it exit.
//
// Shared memory Local\eid3d11_x64r_<game pid>: Control, the ring, the
// readback area (Control::readback_bytes) and the prefetch slots
// (kPrefetchSlots x kPrefetchSlotBytes).
// Events: Local\eid3d11_x64r_work_<pid> (client -> host: records published),
// ..._space_<pid> (host -> client: ring space freed), ..._reply_<pid>
// (synchronous reply ready), ..._present_<pid> (a present completed).

#include <cstddef>
#include <cstdint>

namespace x64remote {

constexpr std::uint32_t kMagic = 0x44524945u; // 'EIRD'
constexpr std::uint32_t kVersion = 15; // 15: combined-1 (s3-store 14 owned item card text + upscale-menu 13 game address space fields); 12: s3-merge border fog (11) + s3-store camp icons and camp placement (11); 10: core-mode F10 overlay fields; 9: S2e blit and S3 both took 8
constexpr std::uint32_t kMaxQueries = 8192;   // query result slots
constexpr std::uint32_t kQueryDataBytes = 96; // >= D3D11_QUERY_DATA_PIPELINE_STATISTICS
constexpr std::uint32_t kFormats = 192;       // CheckFormatSupport table (DXGI_FORMAT values)
constexpr std::uint32_t kFeatures = 20;       // CheckFeatureSupport snapshot (D3D11_FEATURE 0..19)
constexpr std::uint32_t kFeatureBytes = 64;
constexpr std::uint32_t kPrefetchSlots = 16;          // DO_NOT_WAIT readbacks in flight
constexpr std::uint32_t kPrefetchSlotBytes = 64 * 1024; // PrefetchSlot header included
constexpr std::uint32_t kPrefetchLarge = 0xffffffffu;   // PrefetchSlot::bytes: ready, fetch with readback
constexpr std::uint32_t kHarnessAcks = 16;              // core mode: harness step acks in flight (one step runs at a time)
constexpr std::uint32_t kSurfaceRequests = 1024;         // core mode: surface writes the host asked for, in flight
constexpr std::uint32_t kOverlayInputs = 64;             // core mode: window messages for the host's F10 overlay, in flight
// ---- core mode: camp icons (claude/s3-store) ----
constexpr std::uint32_t kCampIconAnswers = 1024;        // core mode: the host icon worker's answers, in flight

// Every record starts with this header; size is the whole record, a multiple
// of 8. Records never wrap: the writer pads to the ring end first.
struct Record {
    std::uint32_t size;
    std::uint16_t op;
    std::uint16_t context; // 0: immediate context (and device calls), else a deferred context index
};

enum class Op : std::uint16_t {
    pad = 0,
    // device
    destroy,               // u32 handle
    create_buffer,         // CreateResource + D3D11_BUFFER_DESC + init
    create_texture2d,      // CreateResource + D3D11_TEXTURE2D_DESC + init
    create_texture3d,      // CreateResource + D3D11_TEXTURE3D_DESC + init
    create_srv,            // CreateView + D3D11_SHADER_RESOURCE_VIEW_DESC
    create_uav,            // CreateView + D3D11_UNORDERED_ACCESS_VIEW_DESC
    create_rtv,            // CreateView + D3D11_RENDER_TARGET_VIEW_DESC
    create_dsv,            // CreateView + D3D11_DEPTH_STENCIL_VIEW_DESC
    create_shader,         // CreateShader + bytecode
    create_input_layout,   // CreateInputLayout + elements + names + bytecode
    create_blend,          // u32 handle + D3D11_BLEND_DESC
    create_depth_stencil,  // u32 handle + D3D11_DEPTH_STENCIL_DESC
    create_rasterizer,     // u32 handle + D3D11_RASTERIZER_DESC
    create_sampler,        // u32 handle + D3D11_SAMPLER_DESC
    create_query,          // CreateQuery
    create_deferred,       // u32 handle, u32 context index
    set_frame_latency,     // u32 latency (IDXGIDevice1)
    // context
    set_shader,            // u32 stage, u32 handle
    set_srvs,              // Slots + u32 handles[count]
    set_samplers,          // Slots + u32 handles[count]
    set_cbs,               // Slots + u32 handles[count]
    set_cbs1,              // Slots + u32 handles[count] + u32 first[count] + u32 num[count]
    set_uavs,              // Slots (stage = compute) + u32 handles[count] + u32 counts[count] (has_counts)
    ia_layout,             // u32 handle
    ia_vertex_buffers,     // Slots + u32 handles[count] + u32 strides[count] + u32 offsets[count]
    ia_index_buffer,       // u32 handle, u32 format, u32 offset
    ia_topology,           // u32 topology
    draw,                  // u32 vertices, u32 start
    draw_indexed,          // u32 indices, u32 start, i32 base
    draw_instanced,        // u32 vertices, u32 instances, u32 start, u32 start instance
    draw_indexed_instanced,// u32 indices, u32 instances, u32 start, i32 base, u32 start instance
    draw_instanced_indirect,         // u32 buffer, u32 offset
    draw_indexed_instanced_indirect, // u32 buffer, u32 offset
    dispatch,              // u32 x, y, z
    dispatch_indirect,     // u32 buffer, u32 offset
    query_begin,           // u32 handle
    query_end,             // u32 handle, u32 slot, u64 serial
    om_targets,            // u32 count, u32 dsv, u32 rtvs[count]
    om_blend,              // u32 handle, u32 has_factor, f32 factor[4], u32 mask
    om_depth_stencil,      // u32 handle, u32 ref
    rs_state,              // u32 handle
    rs_viewports,          // u32 count + D3D11_VIEWPORT[count]
    rs_scissors,           // u32 count + D3D11_RECT[count]
    copy_region,           // CopyRegion
    copy_resource,         // u32 dst, u32 src
    update_subresource,    // Update + tight bytes
    clear_rtv,             // u32 handle, f32 color[4]
    clear_uav_uint,        // u32 handle, u32 values[4]
    clear_uav_float,       // u32 handle, f32 values[4]
    clear_dsv,             // u32 handle, u32 flags, f32 depth, u32 stencil
    generate_mips,         // u32 srv
    execute_list,          // u32 list, u32 restore
    finish_list,           // u32 list handle, u32 restore
    clear_state,
    flush,
    map_write,             // MapWrite + ranges + bytes
    readback,              // Readback (synchronous: the reply carries the HRESULT)
    prefetch,              // Readback (asynchronous DO_NOT_WAIT delivery)
    // swap chain
    create_swap_chain,     // CreateSwapChain
    present,               // u32 swap, u32 interval, u32 flags, u32 reserved, u64 serial
    resize_buffers,        // u32 swap, u32 count, u32 width, u32 height, u32 format, u32 flags
    sync,                  // u64 reply serial: the host replies once it got here
    // neighbour maps (neighbour_remote.h): the host runs the stream
    neighbour_begin,       // NeighbourBegin + encoded inputs
    neighbour_stop,        // u64 epoch (0: any)
    neighbour_take,        // NeighbourTake (synchronous: reply area holds NeighbourTakeReply)
    neighbour_bind,        // NeighbourBind + u32 handles[count] (synchronous)
    neighbour_read,        // NeighbourRead (synchronous: reply area holds the bytes)
    neighbour_uploaded,    // u64 epoch
    // DLSS Super Resolution (NGX lives in the x64 host only)
    dlss_query,            // DlssQuery (synchronous: reply_values[0..1] = optimal render size)
    dlss_evaluate,         // DlssEvaluateRecord, on the immediate context
    dlss_release,          // no payload: release the DLSS feature (DLSS turned off)
    retained,              // RetainedFragment + bytes: retained-scene publication stream (retained_publication.h)
    // game surface uploads: the host converts and uploads (SurfaceStep)
    surface,               // SurfaceStep (+ write: tight source rows)
    // core mode (EI_RENDERER_X64_CORE=1): typed scene commands for the host's Renderer
    scene,                 // SceneFragment + bytes (scene_wire.h codec, scene_core.h command ids)
    count
};

enum Stage : std::uint32_t { vs = 0, hs, ds, gs, ps, cs, stage_count };

struct Slots {
    std::uint32_t stage;
    std::uint32_t start;
    std::uint32_t count;
    std::uint32_t flags; // set_uavs: 1 = counts present
};

// Initial data follows CreateResource: for each of `subresources` a
// SubresourceData, then the packed bytes in the same order (rows tight).
// init == 0: no data (or the data follows as update_subresource chunks;
// then default_usage asks the host to create an IMMUTABLE resource as DEFAULT).
struct CreateResource {
    std::uint32_t handle;
    std::uint32_t init;          // 1 when SubresourceData records follow
    std::uint32_t subresources;
    std::uint32_t default_usage; // 1: IMMUTABLE created as DEFAULT
};
struct SubresourceData {
    std::uint32_t row_pitch;   // tight
    std::uint32_t depth_pitch; // tight
    std::uint32_t bytes;       // packed bytes of this subresource
    std::uint32_t reserved;
};

struct CreateView {
    std::uint32_t handle;
    std::uint32_t resource;
    std::uint32_t has_desc;
    std::uint32_t reserved;
    // followed by the view desc (zeroed when has_desc == 0)
};

struct CreateShader {
    std::uint32_t handle;
    std::uint32_t stage;
    std::uint32_t bytes;
    std::uint32_t reserved;
};

struct InputElement {
    std::uint32_t name_offset; // into the name block
    std::uint32_t semantic_index;
    std::uint32_t format;
    std::uint32_t input_slot;
    std::uint32_t aligned_byte_offset;
    std::uint32_t input_slot_class;
    std::uint32_t instance_data_step_rate;
    std::uint32_t reserved;
};
struct CreateInputLayout {
    std::uint32_t handle;
    std::uint32_t elements;
    std::uint32_t name_bytes;     // NUL-terminated names, padded to 8
    std::uint32_t bytecode_bytes;
};

struct CreateQuery {
    std::uint32_t handle;
    std::uint32_t query;     // D3D11_QUERY
    std::uint32_t misc;
    std::uint32_t slot;      // result slot in Control::queries
};

struct CopyRegion {
    std::uint32_t dst, dst_subresource, x, y, z;
    std::uint32_t src, src_subresource, has_box;
    std::uint32_t box[6]; // left, top, front, right, bottom, back
};

struct Update {
    std::uint32_t dst, subresource, has_box, row_pitch;
    std::uint32_t depth_pitch, bytes, reserved0, reserved1;
    std::uint32_t box[6];
    std::uint32_t reserved2[2];
};

// Dynamic resource write at Unmap: the host maps with map_type (WRITE_DISCARD
// or WRITE_NO_OVERWRITE) and copies each range. Buffers: byte ranges. A
// texture subresource: one range of rows (offset = first row, bytes = rows *
// row_pitch), row_pitch is the client shadow's.
struct MapWrite {
    std::uint32_t resource, subresource, map_type, map_flags;
    std::uint32_t ranges, row_pitch, rows_texture, row_bytes;
};
struct Range {
    std::uint32_t offset;
    std::uint32_t bytes;
};

// Map(READ) of a staging subresource, packed as `rows` rows of `row_bytes`
// per depth slice at `row_pitch`/`depth_pitch` (the client layout).
// readback: the host copies packed bytes [first_byte, first_byte + bytes)
// into the readback area and replies (a large subresource takes several
// requests; first_byte 0 maps and packs it, the others reuse that copy).
// prefetch: when the GPU is done the host packs the subresource into
// prefetch slot `slot` if it fits, then sets the slot's serial.
struct Readback {
    std::uint32_t resource, subresource, map_flags, rows;
    std::uint32_t row_bytes, row_pitch, depth_pitch, depth;
    std::uint32_t first_byte, bytes;
    std::uint32_t slot, serial;
    std::uint64_t reply;     // readback: reply serial
    std::uint64_t reserved;
};

// Header of each prefetch slot; the packed bytes follow it.
struct PrefetchSlot {
    volatile std::uint32_t serial; // the prefetch this slot answers (set last)
    std::uint32_t bytes;           // packed bytes, or kPrefetchLarge
    std::uint32_t reserved[2];
};

struct DlssQuery {
    std::uint32_t out_w, out_h;
    std::int32_t mode; // 0 performance, 1 balanced, 2 quality, 3 ultra performance, 4 DLAA
    std::uint32_t reserved;
    std::uint64_t reply;
};

enum DlssFlags : std::uint32_t { dlss_reset = 1, dlss_hdr = 2, dlss_depth_inverted = 4 };
struct DlssEvaluateRecord {
    std::uint32_t color, depth, motion, output, exposure; // handles (exposure 0: auto exposure)
    float jitter_x, jitter_y, mv_scale_x, mv_scale_y;
    std::uint32_t render_w, render_h, out_w, out_h;
    std::int32_t mode;
    float sharpness;
    std::uint32_t flags; // DlssFlags
    std::uint32_t reserved[2];
};

// ---- game surface uploads -------------------------------------------------
// The x86 renderer sends a game surface's dirty rectangle in the game's own
// pixel format (DirectDraw masks and colour key); the host converts it
// (Source/surface_convert.h) and writes it into the texture the client
// created under its handle. Steps with batch 0 run at once. Steps with
// batch N > 0 come from the game thread while queued scene-thread commands
// may still sample the old contents: the host keeps them, in order, until
// a commit step with batch >= N, which the scene thread writes at the point
// of its command stream where the game thread flushed its uploads.
enum class SurfaceKind : std::uint32_t { write = 0, copy = 1, commit = 2 };
enum SurfaceFlags : std::uint32_t {
    surface_mips = 1,   // write, copy: GenerateMips(dst_srv) after the step
    surface_opaque = 2, // write: alpha forced to 255 after conversion
    surface_whole = 4,  // copy: CopyResource (else level 0 only)
    surface_key_rgb = 8, // write: the colour key compares the R, G and B bits only (UI blits)
};
struct SurfaceStep {
    std::uint32_t kind;  // SurfaceKind
    std::uint32_t flags; // SurfaceFlags
    std::uint64_t batch; // 0: now; else the game-thread batch (commit: the last batch it releases)
    std::uint32_t dst, dst_srv, src; // texture, its SRV (surface_mips), copy source
    std::uint32_t x, y;              // write: destination texel of the first source texel
    std::uint32_t width, height;     // write: texels covered (block-aligned for DXT except at the edge)
    std::uint32_t row_bytes, rows;   // write: tight source rows that follow (block rows for DXT)
    std::uint32_t has_key, key_low, key_high;  // write: colour key (alpha 0 inside the range)
    std::uint32_t pf_flags, fourcc, bit_count; // write: DDPIXELFORMAT of the rows
    std::uint32_t r_mask, g_mask, b_mask, a_mask;
    std::uint32_t reserved;
};

struct CreateSwapChain {
    std::uint32_t handle;
    std::uint32_t back_buffer; // handle the host gives GetBuffer(0)
    std::uint64_t hwnd;
    std::uint32_t width, height, format, stereo;
    std::uint32_t sample_count, sample_quality, usage, buffer_count;
    std::uint32_t scaling, swap_effect, alpha_mode, flags;
};

// ---- neighbour maps -------------------------------------------------------
// The host's neighbour stream (Host/neighbour_host.cpp) builds pieces; the
// client takes one in layout order: neighbour_take serializes the next piece
// on the host and replies with its size and GPU object count; the client
// allocates that many handles and sends neighbour_bind (the host files the
// piece's objects under them); neighbour_read then copies the piece bytes
// through the reply area, area by area.
struct NeighbourBegin {
    std::uint64_t epoch;
    std::uint32_t enabled;
    std::uint32_t bytes;       // encoded inputs that follow (neighbour_remote.h)
};
struct NeighbourTake {
    std::uint64_t epoch;
    std::uint64_t reply;
};
struct NeighbourTakeReply {
    std::uint32_t status;      // 0: no piece ready, 1: a piece, 2: the piece failed to serialize
    std::uint32_t objects;
    std::uint64_t bytes;
    char failure[112];
};
struct NeighbourBind {
    std::uint64_t reply;
    std::uint32_t count;
    std::uint32_t reserved;
};
struct NeighbourRead {
    std::uint64_t reply;
    std::uint64_t offset;
    std::uint32_t bytes;
    std::uint32_t reserved;
};

// ---- retained-scene publication -------------------------------------------
// One message of the stream in order-preserving fragments: the recorder
// writes every fragment of a message before any other retained record, so
// the host appends fragments until `last` and then has one whole message.
struct RetainedFragment {
    std::uint32_t bytes;
    std::uint32_t last;  // 1 on the message's final fragment
};

// ---- core-mode scene messages ---------------------------------------------
// One message (a command id and its scene_wire bytes) in order-preserving
// fragments, as RetainedFragment: every fragment of a message is written
// before any other scene record.
struct SceneFragment {
    std::uint32_t bytes;
    std::uint16_t command; // the message's command id (the same on every fragment)
    std::uint16_t last;    // 1 on the message's final fragment
};

// ---- shared control block -------------------------------------------------

struct AdapterInfo {
    wchar_t description[128]; // wchar_t is 16-bit on both sides
    std::uint32_t vendor, device, subsys, revision;
    std::uint64_t dedicated_video, dedicated_system, shared_system;
    std::uint32_t luid_low;
    std::int32_t luid_high;
    std::uint32_t flags;
    std::uint32_t reserved;
    std::int64_t umd_version; // CheckInterfaceSupport(IDXGIDevice)
};

struct Feature {
    std::int32_t hr;
    std::uint32_t bytes;
    std::uint8_t data[kFeatureBytes];
};

struct Capabilities {
    std::uint32_t feature_level;
    std::uint32_t tearing; // DXGI_FEATURE_PRESENT_ALLOW_TEARING
    std::uint32_t creation_flags;
    std::uint32_t dlss_available; // NGX initialised and DLSS Super Resolution available
    AdapterInfo adapter;
    Feature features[kFeatures];
    std::uint32_t format_support[kFormats];
    std::uint32_t format_support2[kFormats];
    char dlss_reason[128];        // why DLSS is not available (empty when it is)
};

struct MemoryInfo {
    std::uint64_t budget, usage, available_reservation, reservation;
};

struct QuerySlot {
    volatile std::uint64_t serial; // the End this data answers
    std::uint32_t bytes;
    std::uint32_t reserved;
    std::uint8_t data[kQueryDataBytes];
};

// Query result slots are sized by demand. Slots 0..kMaxQueries-1 are
// Control::queries; every further kMaxQueries slots form segment N (N >= 1),
// a shared mapping the client creates when it first needs it, before any
// record names one of its slots, and the host opens on first delivery:
// name kQuerySegmentName with (N, game pid). The segment table only bounds
// pointer storage: kQuerySegments segments are more address space than the
// 32-bit game has, so a failed mapping is the real limit (reported, never
// silent). F11 captures grow the profiler's per-frame query pools well past
// kMaxQueries (crash of 2026-10-24: 0x887A0005 at the second capture).
constexpr std::uint32_t kQuerySegments = 4096;
constexpr std::uint32_t kNoQuerySlot = 0xFFFFFFFFu;
constexpr wchar_t kQuerySegmentName[] = L"Local\\eid3d11_x64r_q%u_%lu";
constexpr std::size_t kQuerySegmentBytes = std::size_t(kMaxQueries) * sizeof(QuerySlot);

#pragma warning(push)
#pragma warning(disable : 4324) // padding from alignas: intended (cache lines)
struct alignas(64) Control {
    std::uint32_t magic, version;
    std::uint32_t ring_bytes, control_bytes;
    std::uint32_t requested_flags; // D3D11CreateDevice flags from the client
    std::uint32_t frames_ahead;
    std::uint32_t readback_bytes;  // readback area after the ring
    std::uint32_t core_mode;       // 1: core mode (scene records; the client has no D3D11 device)
    // client -> host
    alignas(64) volatile std::int64_t write_pos; // published
    volatile long host_sleeping;
    // host -> client
    alignas(64) volatile std::int64_t read_pos;  // consumed
    volatile long space_waiting;
    volatile long reply_waiting;
    volatile long present_waiting;
    alignas(64) volatile long host_ready;        // 1: capabilities valid, 2: failed
    volatile long device_removed;                // HRESULT once removed, else 0
    volatile std::uint64_t presents_done;
    volatile std::uint64_t reply_serial;
    volatile long reply_hr;
    std::uint32_t reserved1;
    char host_reason[128];                       // why the host failed the device (written before device_removed)
    std::uint32_t reply_values[4];               // small reply payload (dlss_query: render width, height)
    // frame statistics and video memory (seqlocked, host-written)
    alignas(64) volatile long stats_sequence;
    std::uint32_t present_count, present_refresh_count, sync_refresh_count, reserved2;
    std::int64_t sync_qpc_time, sync_gpu_time;
    MemoryInfo local, non_local;
    std::uint64_t host_frames;
    // neighbour stream status (seqlocked, host-written after each neighbour
    // record and each present): the epoch it describes, whether it settled,
    // the pieces not yet taken (known once the layout was read) and its error.
    alignas(64) volatile long neighbour_sequence;
    std::uint32_t neighbour_flags;  // 1: settled, 2: remaining known
    std::uint64_t neighbour_epoch;
    std::uint32_t neighbour_remaining, reserved3;
    char neighbour_error[232];
    // core mode: the test harness (test_harness.h). The game process writes
    // its result directory before the first present (empty: harness off);
    // the host Renderer runs the renderer verbs (screenshot, capture, profile,
    // camera path, wait_ready) and acks each finished step here, in order;
    // the game thread retires them at its next present.
    char harness_directory[520];                 // UTF-8, NUL-terminated
    alignas(64) volatile long harness_acks_written; // acks published (slot = count % kHarnessAcks)
    volatile long overlay_mirror_resets;         // core mode: the host dropped stale cached UI quads (the game clears its mirror)
    // core mode: completed visual surfaces the host's resolver missed (id,
    // generation). The game process sends their write at its next present.
    // Host-written ring: slot = count % kSurfaceRequests, published by count.
    // core mode: the camp popup answers of the host's UI passes, as counts the
    // game process applies at its next present: [0] popup not published,
    // [1] popup failed, [2] camp art failed (camp_inventory.cpp).
    volatile long camp_answers[3];
    // core mode: the host Renderer's progress-movie decode request (the game
    // process decodes with the game's binkw32 and answers ProgressFrames);
    // serial 0: none. Written before progress_serial is published.
    std::uint32_t progress_width, progress_height, progress_frames, progress_shown;
    char progress_path[520];
    volatile std::uint64_t progress_serial;
    volatile long surface_requests_written;
    struct SurfaceRequest {
        std::uint64_t id, generation;
    } surface_requests[kSurfaceRequests];

    // core mode: the F10 overlay runs on the host Renderer (DebugOverlay).
    // Host -> game: overlay_flags bit 0 = it takes keyboard and mouse input
    // (the game process swallows them, as the overlay's window hook does);
    // config_saved counts the host's ini saves (the game process reloads the
    // ini and applies it); a display Apply (mode, client size) by serial.
    volatile long overlay_flags;
    volatile long config_saved;
    volatile long display_apply_serial;
    std::int32_t display_mode, display_width, display_height;
    // Game -> host: the game process's address space in use and its largest
    // free block (MB; 0: not sampled yet), for the F10 memory line. Sampled
    // at most once a second while the overlay takes input.
    volatile long game_va_used_mb, game_va_largest_free_mb;
    // Host -> game: the neighbour border fog the host's neighbour stream
    // published (neighbour_maps.h NeighbourBorderFog as 17 doubles: active,
    // edges, range, contacts lo, contacts hi) and its host revision, under a
    // seqlock (odd while written). The game's per-entity fog continues the
    // band with it, as the host's terrain shaders do.
    alignas(8) volatile long border_fog_sequence;
    std::uint32_t reserved4;
    std::uint64_t border_fog_revision;
    double border_fog[17];
    // Game -> host: the window messages the overlay reads (wheel always;
    // keys and characters while it takes input). Slot = count % kOverlayInputs.
    volatile long overlay_inputs_written;
    struct OverlayInput {
        std::uint32_t message, wparam;
        std::int32_t lparam, reserved;
    } overlay_inputs[kOverlayInputs];

    // ---- core mode: camp icons (claude/s3-store) ----
    // The host's icon worker (Host/core_camp.cpp) answers each CampIconRequest:
    // its canvas (0: the icon failed; the host log names why) and whether its
    // alpha is fully opaque. Host-written ring: slot = count % kCampIconAnswers,
    // published by count; the game thread reads it while the camp draws.
    volatile long camp_icon_answers_written;
    struct CampIconAnswer {
        std::uint64_t serial;
        std::uint32_t canvas, opaque;
    } camp_icon_answers[kCampIconAnswers];

    struct HarnessAck {
        std::uint32_t entry_id;
        std::uint32_t ok;
        char detail[504];
    } harness_acks[kHarnessAcks];
    alignas(64) Capabilities caps;
    alignas(64) QuerySlot queries[kMaxQueries];
};

#pragma warning(pop)

static_assert(sizeof(Record) == 8, "wire");
static_assert(sizeof(Slots) == 16, "wire");
static_assert(sizeof(CreateResource) == 16, "wire");
static_assert(sizeof(SubresourceData) == 16, "wire");
static_assert(sizeof(CreateView) == 16, "wire");
static_assert(sizeof(CreateShader) == 16, "wire");
static_assert(sizeof(InputElement) == 32, "wire");
static_assert(sizeof(CreateInputLayout) == 16, "wire");
static_assert(sizeof(CreateQuery) == 16, "wire");
static_assert(sizeof(CopyRegion) == 56, "wire");
static_assert(sizeof(Update) == 64, "wire");
static_assert(sizeof(MapWrite) == 32, "wire");
static_assert(sizeof(Readback) == 64, "wire");
static_assert(sizeof(PrefetchSlot) == 16, "wire");
static_assert(sizeof(CreateSwapChain) == 64, "wire");
static_assert(sizeof(SurfaceStep) == 96, "wire");
static_assert(sizeof(DlssQuery) == 24, "wire");
static_assert(sizeof(DlssEvaluateRecord) == 72, "wire");
static_assert(sizeof(AdapterInfo) == 256 + 16 + 24 + 16 + 8, "wire");
static_assert(sizeof(QuerySlot) == 112, "wire");
static_assert(sizeof(NeighbourBegin) == 16, "wire");
static_assert(sizeof(NeighbourTake) == 16, "wire");
static_assert(sizeof(NeighbourTakeReply) == 128, "wire");
static_assert(sizeof(NeighbourBind) == 16, "wire");
static_assert(sizeof(NeighbourRead) == 24, "wire");
static_assert(sizeof(RetainedFragment) == 8, "wire");
static_assert(sizeof(SceneFragment) == 8, "wire");
static_assert(offsetof(Control, write_pos) == 64, "wire");
static_assert(sizeof(Control) % 64 == 0, "wire");

} // namespace x64remote
