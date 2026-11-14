#pragma once
// Wire between the game process and the x64 render host (core mode): the
// game process (x64_remote_link.cpp) writes the retained-scene publication
// stream and typed scene commands into one shared-memory ring; the host
// (Host/remote_host.cpp, core_host.cpp) reads them in order and runs its
// Renderer on the real device. Fixed-width fields only; no pointer crosses
// except the game HWND. Replies come back through shared memory only; the
// host opens the game process just to see it exit.
//
// Shared memory Local\eid3d11_x64r_<game pid>: Control, the ring and the
// readback area (Control::readback_bytes).
// Events: Local\eid3d11_x64r_work_<pid> (game -> host: records published),
// ..._space_<pid> (host -> game: ring space freed), ..._reply_<pid>
// (synchronous reply ready), ..._present_<pid> (a present completed).

#include <cstddef>
#include <cstdint>

namespace x64remote {

constexpr std::uint32_t kMagic = 0x44524945u; // 'EIRD'
constexpr std::uint32_t kVersion = 36; // 36: authored Astral terrain corner masks in asset publication; 35: owned unknown-map FIG/MapG cover; 34: same-frame map marker controls and border tint; 33: standalone loading-screen map cache; 32: footstep native-contact removal reserved; 30: owned world-map preview commands; 28: authored-family policy with name provenance (neighbor piece5); 27: GameStatus::addon_notice; 26: terrain family/neighbor policy (separate integration); 25: Preview::portrait_hair; 24: Preview::portrait_lighting; 23: game-owned camera pan availability in GameStatus; 22: host_window_work removed again; 21: Control::host_window_work, release_delay_us; 20: cave flag in NativeWorldLightingFrame (#208); 19: zone-mask HitTestFrame (#200); 18: candidate-1025 (S7 17 + grass-cull WorldScene camera anchor height); 17: S7, the call-stream ops, queries, prefetch and neighbour status removed; 16: game_clean_exit; 15: combined-1
constexpr std::uint32_t kHarnessAcks = 16;              // harness step acks in flight (one step runs at a time)
constexpr std::uint32_t kSurfaceRequests = 1024;         // surface writes the host asked for, in flight
constexpr std::uint32_t kOverlayInputs = 64;             // window messages for the host's F10 overlay, in flight
constexpr std::uint32_t kCampIconAnswers = 1024;         // the host icon worker's answers, in flight

// Every record starts with this header; size is the whole record, a multiple
// of 8. Records never wrap: the writer pads to the ring end first.
struct Record {
    std::uint32_t size;
    std::uint16_t op;
    std::uint16_t reserved;
};

enum class Op : std::uint16_t {
    pad = 0,
    retained,              // RetainedFragment + bytes: retained-scene publication stream (retained_publication.h)
    scene,                 // SceneFragment + bytes (scene_wire.h codec, scene_core.h command ids)
    count
};

// DLSS evaluation inputs (host-internal: host_bridge.cpp hands them to Dlss).
enum DlssFlags : std::uint32_t { dlss_reset = 1, dlss_hdr = 2, dlss_depth_inverted = 4 };
struct DlssEvaluateRecord {
    std::uint32_t color, depth, motion, output, exposure; // unused in core mode (resources are passed directly)
    float jitter_x, jitter_y, mv_scale_x, mv_scale_y;
    std::uint32_t render_w, render_h, out_w, out_h;
    std::int32_t mode;
    float sharpness;
    std::uint32_t flags; // DlssFlags
    std::uint32_t reserved[2];
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

struct Capabilities {
    std::uint32_t feature_level;
    std::uint32_t tearing; // DXGI_FEATURE_PRESENT_ALLOW_TEARING
    std::uint32_t creation_flags;
    std::uint32_t dlss_available; // NGX initialised and DLSS Super Resolution available
    AdapterInfo adapter;
    char dlss_reason[128];        // why DLSS is not available (empty when it is)
};

struct MemoryInfo {
    std::uint64_t budget, usage, available_reservation, reservation;
};

#pragma warning(push)
#pragma warning(disable : 4324) // padding from alignas: intended (cache lines)
struct alignas(64) Control {
    std::uint32_t magic, version;
    std::uint32_t ring_bytes, control_bytes;
    std::uint32_t requested_flags; // D3D11CreateDevice flags from the game process
    std::uint32_t frames_ahead;
    std::uint32_t readback_bytes;  // readback area after the ring
    std::uint32_t core_mode;       // 1: core mode (the only mode since S7; a host refuses 0)
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
    volatile long release_delay_us;              // latency_pacing: the game waits this long after each present wait (host-set)
    char host_reason[128];                       // why the host failed the device (written before device_removed)
    // video memory (seqlocked, host-written) and the host's presented frames
    alignas(64) volatile long stats_sequence;
    std::uint32_t reserved2;
    MemoryInfo local, non_local;
    std::uint64_t host_frames;
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
    // Game -> host: 1 once the game process left through its clean exit
    // (~DD). Read by the host after the game exited: the host, the only ini
    // writer in core mode, then clears the over-budget marker.
    volatile long game_clean_exit;
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
};

#pragma warning(pop)

static_assert(sizeof(Record) == 8, "wire");
static_assert(sizeof(DlssEvaluateRecord) == 72, "wire");
static_assert(sizeof(AdapterInfo) == 256 + 16 + 24 + 16 + 8, "wire");
static_assert(sizeof(RetainedFragment) == 8, "wire");
static_assert(sizeof(SceneFragment) == 8, "wire");
static_assert(offsetof(Control, write_pos) == 64, "wire");
static_assert(sizeof(Control) % 64 == 0, "wire");

} // namespace x64remote
