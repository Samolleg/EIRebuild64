#pragma once
// Remote D3D11 device (EI_RENDERER_X64_REMOTE=1 with EI_RENDERER_X64_HOST):
// the renderer gets proxy device, context and (through the DXGI factory of
// the proxy adapter) swap-chain objects. No real D3D11 device exists in the
// game process; the x64 host replays every call (x64_remote_protocol.h).

#include <d3d11.h>
#include <cstdint>

namespace x64remote {

bool remote_requested() noexcept;

// Player option x64_render_host (eid3d11.ini [graphics], default off) or
// the test override EI_RENDERER_X64_REMOTE=1. Called once, before any
// device exists. Returns false (and leaves a notice) when the previous
// start of the host never reached its first frame (the option is turned
// off: rollback), or when the host cannot run here.
bool remote_wanted() noexcept;
// Why the host cannot run on this machine ("needs 64-bit Windows", "64-bit
// host file missing"), or null. The settings grey the option out with it.
const char* host_unavailable_reason() noexcept;
// Private bytes of the running x64 host process, from the game process
// (call-stream or core link); 0 when no host runs. The F10 memory line.
std::uint64_t host_private_bytes() noexcept;
// After create_remote_device failed in this session: turns the option off
// in the ini and leaves a notice naming the reason; the caller continues
// on the standard local device (user-approved, visible fallback).
void remote_start_failed(const char* reason) noexcept;
// The notice to show (rollback or start failure), or null.
const char* remote_notice() noexcept;

// Replaces D3D11CreateDevice. Fails (with one log line and a bridge event)
// when the host cannot be started; there is no local fallback.
HRESULT create_remote_device(HWND game_window, UINT flags, ID3D11Device** device, D3D_FEATURE_LEVEL* level,
                             ID3D11DeviceContext** context) noexcept;

// Declares that the next Map of `resource` on `context` writes only
// [offset, offset + bytes) of the buffer (the renderer's append rings). In
// remote mode only those bytes travel and no x86 copy of the buffer is
// kept; a NO_OVERWRITE map without it is reported. No-op otherwise.
void declare_map_range(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT offset, UINT bytes) noexcept;

// ---- DLSS Super Resolution (remote device only; NGX runs in the x64 host) ----
// All return false / E_NOTIMPL when the remote device is off.
struct DlssCaps {
    bool available;
    char reason[128]; // why not, when unavailable
};
// The host's snapshot at connect: NGX initialised and DLSS available on
// this GPU and driver.
bool dlss_caps(DlssCaps& out) noexcept;
// Optimal render size for an output size and mode (0 performance, 1
// balanced, 2 quality, 3 ultra performance, 4 DLAA = output size).
// Synchronous the first time per (w, h, mode), cached afterwards.
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
    // (2*jx/render_w, -2*jy/render_h). (DLSS Programming Guide 3.7; the
    // NGX debug hotkey CTRL+ALT+F9 cycles sign variants to check it.)
    float jitter_x, jitter_y;
    float mv_scale_x, mv_scale_y; // usually 1, 1 when motion is in render pixels
    UINT render_w, render_h, out_w, out_h;
    int mode;
    float sharpness;
    bool reset;
    bool hdr;
    bool depth_inverted;
};
// Writes a record in call order (immediate context only). The host
// recreates its feature when render size, output size, mode, hdr,
// depth_inverted or auto exposure change. NGX preserves the immediate
// D3D11 context state (Programming Guide 5.2.5). A failed evaluation fails
// the device (the game closes naming it). Motion vectors are taken as
// render resolution (MVLowRes) and without jitter (no MVJittered).
HRESULT dlss_evaluate(ID3D11DeviceContext* ctx, const DlssEvaluate& e) noexcept;
// Releases the host's DLSS feature and its memory (DLSS turned off), in call
// order. The next dlss_evaluate creates a new feature. No-op when the remote
// device is off.
void dlss_release() noexcept;
// True once the host has presented at least one frame (the renderer defers
// host DLSS until then; see Renderer::render_dlss). False without the remote device.
bool host_presented() noexcept;

// Why the remote device was lost (the host died, reported removal, ...),
// or null; the device-removed message names it.
const char* remote_lost_reason() noexcept;

} // namespace x64remote
