// Surface protocol, x86 side (x64_remote_surfaces.h).
#include "x64_remote_surfaces.h"
#include "x64_remote_objects.h"

#include <atomic>

namespace x64remote {
namespace {
std::atomic<std::uint64_t> g_open_batch{1};

SurfaceStep step_of(SurfaceKind kind, bool queued) noexcept {
    SurfaceStep step{};
    step.kind = static_cast<std::uint32_t>(kind);
    step.batch = queued ? g_open_batch.load(std::memory_order_relaxed) : 0u;
    return step;
}
} // namespace

void surface_write(bool queued, const SurfaceWrite& write) noexcept {
    if (!write.texture || !write.format || !write.rows || !write.row_bytes) return;
    const bool blocks = (write.format->dwFlags & DDPF_FOURCC) != 0;
    const UINT rows_per_texel_row = blocks ? 4u : 1u;
    SurfaceStep step = step_of(SurfaceKind::write, queued);
    step.dst = handle_of(write.texture);
    step.dst_srv = handle_of(write.srv);
    step.x = write.x;
    step.width = write.width;
    step.row_bytes = write.row_bytes;
    if (write.color_key) {
        step.has_key = 1;
        step.key_low = write.color_key->dwColorSpaceLowValue;
        step.key_high = write.color_key->dwColorSpaceHighValue;
    }
    step.pf_flags = write.format->dwFlags;
    step.fourcc = write.format->dwFourCC;
    step.bit_count = write.format->dwRGBBitCount;
    step.r_mask = write.format->dwRBitMask;
    step.g_mask = write.format->dwGBitMask;
    step.b_mask = write.format->dwBBitMask;
    step.a_mask = write.format->dwRGBAlphaBitMask;
    // Row bands that fit one record; a single row larger than a chunk
    // cannot occur for game surfaces (8 MB ring chunk), but is reported.
    const std::uint32_t room = chunk_bytes() - static_cast<std::uint32_t>(sizeof(SurfaceStep));
    if (write.row_bytes > room) { unsupported("a surface row larger than a ring chunk"); return; }
    const UINT band = (std::max)(1u, room / write.row_bytes);
    for (UINT row = 0; row < write.rows; row += band) {
        const UINT rows = (std::min)(band, write.rows - row);
        const UINT top = row * rows_per_texel_row;
        step.y = write.y + top;
        step.height = (std::min)(write.height - top, rows * rows_per_texel_row);
        step.rows = rows;
        const bool last = row + rows >= write.rows;
        step.flags = (last && write.generate_mips ? surface_mips : 0u) | (write.force_opaque ? surface_opaque : 0u) |
            (write.key_rgb_only ? surface_key_rgb : 0u);
        const std::uint32_t bytes = rows * write.row_bytes;
        Writer w(Op::surface, 0, static_cast<std::uint32_t>(sizeof(step)) + bytes);
        w.put(step);
        std::uint8_t* out = w.take(bytes);
        const std::uint8_t* in = write.source + static_cast<std::size_t>(row) * write.source_pitch;
        if (write.source_pitch == write.row_bytes) {
            std::memcpy(out, in, bytes);
        } else {
            for (UINT y = 0; y < rows; ++y)
                std::memcpy(out + static_cast<std::size_t>(y) * write.row_bytes,
                            in + static_cast<std::size_t>(y) * write.source_pitch, write.row_bytes);
        }
        count_update(bytes);
    }
}

void surface_copy(bool queued, ID3D11Texture2D* texture, ID3D11ShaderResourceView* srv, ID3D11Texture2D* source,
                  bool whole, bool generate_mips) noexcept {
    if (!texture || !source) return;
    SurfaceStep step = step_of(SurfaceKind::copy, queued);
    step.flags = (whole ? surface_whole : 0u) | (generate_mips ? surface_mips : 0u);
    step.dst = handle_of(texture);
    step.dst_srv = handle_of(srv);
    step.src = handle_of(source);
    Writer w(Op::surface, 0, sizeof(step));
    w.put(step);
}

std::uint64_t surface_close_batch() noexcept {
    return g_open_batch.fetch_add(1, std::memory_order_relaxed);
}

void surface_commit(std::uint64_t batch) noexcept {
    SurfaceStep step{};
    step.kind = static_cast<std::uint32_t>(SurfaceKind::commit);
    step.batch = batch;
    Writer w(Op::surface, 0, sizeof(step));
    w.put(step);
}

} // namespace x64remote
