#pragma once
// The game process's link to the x64 render host (core mode): launches the
// host, owns the shared control block and the record ring (the state
// channel), and writes records in order from any thread. See
// x64_remote_protocol.h.

#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <string>
#include "x64_remote_protocol.h"

struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace x64remote {

// The host executable: EI_RENDERER_X64_HOST, else eid3d11_x64_host.exe next
// to this DLL (the shipped layout).
std::wstring host_path() noexcept;

// Launches the host and waits for its capability snapshot. On failure
// `error` names the reason.
bool link_start(HWND game_window, UINT device_flags, char* error, std::size_t error_bytes) noexcept;
Control& control() noexcept;
// Largest payload one record may carry (a quarter of the ring).
std::uint32_t chunk_bytes() noexcept;
// Shared readback area (written by the host under a reply) and its size.
const std::uint8_t* readback_area() noexcept;
std::uint32_t readback_bytes() noexcept;
// True once the host died or reported device removal.
HRESULT link_removed() noexcept;
// EI_RENDERER_X64_CORE=1: test-only override (harness routes) that runs core
// mode without the ini option.
bool core_requested() noexcept;
// Core mode for this start: the player option x64_render_host (eid3d11.ini
// [graphics]) or the test override. Called once, before any device exists.
// False, with a notice, when the previous start never reached the host's
// first frame (the option is turned off: rollback) or when the host cannot
// run here; the game then runs the standard 32-bit renderer.
bool core_wanted() noexcept;
// Why the host cannot run on this machine ("needs 64-bit Windows", "64-bit
// host file missing"), or null. The settings grey the option out with it.
const char* host_unavailable_reason() noexcept;
// The rollback or unavailability notice to show (90 s), or null.
const char* remote_notice() noexcept;
// Private bytes of the running x64 host process, from the game process; 0
// when no host runs. The F10 memory line.
std::uint64_t host_private_bytes() noexcept;
// Why the link was lost (the host died, reported removal, ...), or null;
// the device-removed message names it.
const char* remote_lost_reason() noexcept;
// True while core mode runs: the link started for scene records and the
// game process has no D3D11 device.
bool core_active() noexcept;
// Core mode, the game's clean exit (~DD): tells the host, which clears the
// over-budget marker in the ini it alone writes once the game has exited.
void signal_clean_exit() noexcept;
// Core mode: one scene message (scene_core.h command id, scene_wire bytes)
// as Op::scene fragments, with no other scene record between them.
void scene_message(std::uint16_t command, const std::uint8_t* data, std::size_t bytes) noexcept;
// Picks up a host failure (exit, removal, failed creation) into the lost
// state; called once per Present.
void check_host() noexcept;

// ---- DLSS Super Resolution: NGX lives in the x64 host only ----
// The host Renderer implements these (Host/host_bridge.cpp); in the game
// process they report DLSS unavailable.
struct DlssCaps {
    bool available;
    char reason[128]; // why not, when unavailable
};
// NGX initialised and DLSS available on this GPU and driver.
bool dlss_caps(DlssCaps& out) noexcept;
// Optimal render size for an output size and mode (0 performance, 1
// balanced, 2 quality, 3 ultra performance, 4 DLAA = output size).
bool dlss_render_size(UINT out_w, UINT out_h, int mode, UINT& render_w, UINT& render_h) noexcept;
struct DlssEvaluate {
    ID3D11Resource* color;    // render-res colour input (SRV-capable texture)
    ID3D11Resource* depth;    // render-res depth (D32/R32 typeless depth texture)
    ID3D11Resource* motion;   // render-res RG16F motion vectors in render pixels, current -> previous (NGX convention)
    ID3D11Resource* output;   // output-res texture with UAV bind
    ID3D11Resource* exposure; // may be null: auto exposure
    // Passed unchanged as NGX InJitterOffsetX/Y: the jitter applied to the
    // projection this frame, in render pixels, between -0.5 and +0.5, in the
    // motion-vector system (origin top left: +x right, +y down). With a D3D
    // projection (NDC +y up) a pixel jitter (jx, jy) is the NDC offset
    // (2*jx/render_w, -2*jy/render_h). (DLSS Programming Guide 3.7.)
    float jitter_x, jitter_y;
    float mv_scale_x, mv_scale_y; // usually 1, 1 when motion is in render pixels
    UINT render_w, render_h, out_w, out_h;
    int mode;
    float sharpness;
    bool reset;
    bool hdr;
    bool depth_inverted;
};
// One evaluation on the immediate context. The host recreates its feature
// when render size, output size, mode, hdr, depth_inverted or auto exposure
// change; NGX preserves the immediate context state (Programming Guide
// 5.2.5). Motion vectors are render resolution (MVLowRes), unjittered.
HRESULT dlss_evaluate(ID3D11DeviceContext* ctx, const DlssEvaluate& e) noexcept;
// The D3D12 backend: one evaluation recorded into list (D3D12Frame::
// record_external). resources: colour, depth, motion, output, already in
// their states; the D3D11 pointers of e are not read.
HRESULT dlss_evaluate_d3d12(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources, const DlssEvaluate& e) noexcept;
// Releases the host's DLSS feature and its memory (DLSS turned off).
void dlss_release() noexcept;
// DLSS Frame Generation (D3D12 backend in the host only). One generated
// frame between the previous and this real frame. Matrices are row-major
// for row vectors (v' = v * M, the SDK's "row-major, post-multiplication"),
// without jitter. resources: backbuffer (the real frame), depth, motion,
// HUD-less, interpolated output; the inputs already in shader-read state,
// the output in unordered access (D3D12Frame::record_external).
struct DlssgEvaluate {
    UINT width, height;              // backbuffer = output size
    UINT render_w, render_h;         // depth and motion size
    float clip_to_prev_clip[16], prev_clip_to_clip[16], view_to_clip[16], clip_to_view[16];
    float position[3], up[3], right[3], forward[3];
    float near_plane, far_plane, fov_y, aspect;
    float jitter_x, jitter_y;        // clip space
    float mv_scale_x, mv_scale_y;
    bool reset;
    bool depth_inverted;
};
struct DlssgCaps { bool available; int multi_frame_max; char reason[160]; };
bool dlssg_caps(DlssgCaps& out) noexcept;
HRESULT dlssg_evaluate_d3d12(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources, const DlssgEvaluate& e) noexcept;
// Releases the frame generation feature (turned off; NGX stays initialised).
void dlssg_release() noexcept;
// NGX's VRAM estimate for the feature at a size (0: unknown).
std::uint64_t dlssg_vram_estimate(UINT width, UINT height, UINT render_w, UINT render_h) noexcept;
// True once the host has presented at least one frame (the renderer defers
// host DLSS until then; see Renderer::render_dlss).
bool host_presented() noexcept;

// One record: the constructor takes the ring lock and reserves the whole
// record, put() fills it in order, the destructor completes it.
class Writer {
public:
    Writer(Op op, std::uint32_t payload_bytes) noexcept;
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    void put(const void* data, std::uint32_t bytes) noexcept {
        std::memcpy(cursor_, data, bytes);
        cursor_ += bytes;
    }
    template <class T> void put(const T& value) noexcept { put(&value, sizeof(T)); }
    void u32(std::uint32_t value) noexcept { put(value); }
    // Space for `bytes` the caller fills directly.
    std::uint8_t* take(std::uint32_t bytes) noexcept {
        std::uint8_t* at = cursor_;
        cursor_ += bytes;
        return at;
    }
    // Ring position just past this record (deferred frees).
    std::int64_t end_position() const noexcept { return end_; }

private:
    std::uint8_t* cursor_ = nullptr;
    std::int64_t end_ = 0;
};

// Makes every written record visible to the host.
void publish() noexcept;
// Synchronous request: next_reply_serial() takes the reply lock and returns
// the serial to put in the request record; wait_reply() publishes and
// waits for the host's reply (its HRESULT); end_reply() releases the lock
// once the caller has read the readback area. One request in flight.
std::uint64_t next_reply_serial() noexcept;
HRESULT wait_reply(std::uint64_t serial) noexcept;
void end_reply() noexcept;
// Present bookkeeping: waits until at most frames_ahead presents are
// queued ahead of the host.
void wait_presents(std::uint64_t serial) noexcept;

// Once per present: writes the two-second report line.
void report_frame() noexcept;

// One line in x86_remote.log (next to the host exe) and the debugger.
void log(const char* format, ...) noexcept;
// A named failure: the device is lost with `reason` and the game closes at
// the next Present through the named box.
void lose_device(const char* reason) noexcept;

} // namespace x64remote
