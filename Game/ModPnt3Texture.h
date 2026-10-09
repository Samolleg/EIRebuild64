#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Pnt3Texture
{
// PNT3 is a premultiplied BGRA layer stream, not a hardware surface format.
// Keep the source resource immutable and return an aligned, ordinary MMP image.
inline bool Decode(const void* source, size_t size, std::vector<uint32_t>& output,
                   const char*& reason)
{
    output.clear();
    reason = "header";
    if (!source || size < 76) return false;
    uint32_t h[19];
    std::memcpy(h, source, sizeof(h));
    if (h[0] != 0x00504d4d || h[4] != 0x33544e50 || h[5] != 32) return false;
    const uint64_t pixels = uint64_t(h[1]) * h[2];
    reason = "dimensions";
    // Bound allocation before expanding an untrusted compressed resource.
    if (!h[1] || !h[2] || pixels % 4 || pixels > 64u * 1024u * 1024u) return false;
    const uint64_t offset = uint64_t(76) + h[18];
    reason = "payload-size";
    if (offset > size || h[3] > size - offset) return false;
    const auto* data = static_cast<const uint8_t*>(source) + size_t(offset);
    // Validate the complete stream before allocating or publishing any pixels.
    size_t at = 0, written = 0;
    while (written < size_t(pixels))
    {
        reason = "truncated-run";
        if (h[3] - at < 4) return false;
        uint32_t word;
        std::memcpy(&word, data + at, 4);
        if (word && !(word >> 24))
        {
            reason = "skip-range";
            if (word % 16 || word / 4 > size_t(pixels) - written) return false;
            written += word / 4;
            at += 4;
        }
        else
        {
            reason = "literal-range";
            if (h[3] - at < 16 || size_t(pixels) - written < 4) return false;
            written += 4;
            at += 16;
        }
    }
    reason = "trailing-data";
    if (at != h[3]) return false;
    output.assign(19 + size_t(pixels), 0);
    const uint32_t header[19] = {
        0x00504d4d, h[1], h[2], 1, 0x8888, 32,
        0xff000000, 24, 8, 0x00ff0000, 16, 8,
        0x0000ff00, 8, 8, 0x000000ff, 0, 8, 0
    };
    std::memcpy(output.data(), header, sizeof(header));
    at = written = 0;
    while (written < size_t(pixels))
    {
        uint32_t word;
        std::memcpy(&word, data + at, 4);
        if (word && !(word >> 24))
        {
            written += word / 4;
            at += 4;
            continue;
        }
        for (size_t lane = 0; lane < 4; ++lane)
        {
            uint32_t pixel;
            std::memcpy(&pixel, data + at + lane * 4, 4);
            const uint32_t alpha = pixel >> 24;
            uint32_t straight = alpha << 24;
            if (alpha)
                for (unsigned shift = 0; shift < 24; shift += 8)
                {
                    const uint32_t c = (((pixel >> shift) & 255) * 255 + alpha / 2) / alpha;
                    straight |= (c > 255 ? 255 : c) << shift;
                }
            output[19 + written++] = straight;
        }
        at += 16;
    }
    reason = "ok";
    return true;
}
}
