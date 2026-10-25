#pragma once
// Game surface uploads in remote mode (x86 side of the surface protocol,
// x64_remote_protocol.h SurfaceStep). The renderer sends raw game-format
// rows; the host converts and uploads them, so the game process neither
// converts pixels nor keeps an upload copy, and records no deferred context.
#include <windows.h>
#include <ddraw.h>
#include <d3d11.h>

#include <cstdint>

namespace x64remote {

// One dirty rectangle of a game surface: `rows` x `row_bytes` source bytes
// at `source` (`source_pitch` apart; block rows for DXT), landing at texel
// (x, y) of `texture` and covering width x height texels.
struct SurfaceWrite {
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* srv; // GenerateMips target when generate_mips
    UINT x, y, width, height;
    const std::uint8_t* source;
    UINT source_pitch, row_bytes, rows;
    const DDPIXELFORMAT* format;
    const DDCOLORKEY* color_key; // null: unkeyed
    bool generate_mips;
    bool force_opaque;
    bool key_rgb_only; // UI blit keys: compare only the colour bits
};

// queued: a game-thread step of the open batch (applied at its commit);
// otherwise applied by the host at once, in ring order. Large rectangles
// travel as several row-band steps; mips follow the last one.
void surface_write(bool queued, const SurfaceWrite& write) noexcept;
// Level 0 (or the whole resource) of `source` into `texture`.
void surface_copy(bool queued, ID3D11Texture2D* texture, ID3D11ShaderResourceView* srv, ID3D11Texture2D* source,
                  bool whole, bool generate_mips) noexcept;
// Game thread: ends the open batch and returns its id for surface_commit.
std::uint64_t surface_close_batch() noexcept;
// The scene thread's point in the stream where batches up to `batch` apply.
void surface_commit(std::uint64_t batch) noexcept;

} // namespace x64remote
