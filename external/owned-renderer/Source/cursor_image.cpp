#include "cursor_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace owned {
namespace {

std::uint8_t expand(std::uint32_t raw, std::uint32_t mask) noexcept {
    if (!mask) return 0;
    unsigned shift = 0;
    while (((mask >> shift) & 1u) == 0) ++shift;
    const std::uint32_t maximum = mask >> shift;
    return static_cast<std::uint8_t>((((raw & mask) >> shift) * 255u + maximum / 2) / maximum);
}

// EPX / Scale2x: each pixel becomes four, taking a neighbour's value where two
// orthogonal neighbours agree, which smooths stair steps without blurring.
void epx(const std::vector<std::uint32_t>& in, std::uint32_t w, std::uint32_t h,
         std::vector<std::uint32_t>& out) {
    out.assign(static_cast<std::size_t>(w) * h * 4, 0);
    const auto at = [&](int x, int y) {
        x = std::clamp(x, 0, int(w) - 1); y = std::clamp(y, 0, int(h) - 1);
        return in[static_cast<std::size_t>(y) * w + x];
    };
    const std::size_t ow = static_cast<std::size_t>(w) * 2;
    for (int y = 0; y < int(h); ++y)
        for (int x = 0; x < int(w); ++x) {
            const std::uint32_t p = at(x, y), a = at(x, y - 1), b = at(x + 1, y),
                                c = at(x - 1, y), d = at(x, y + 1);
            std::uint32_t e0 = p, e1 = p, e2 = p, e3 = p;
            if (c == a && c != d && a != b) e0 = a;
            if (a == b && a != c && b != d) e1 = b;
            if (d == c && d != b && c != a) e2 = c;
            if (b == d && b != a && d != c) e3 = d;
            out[(2 * y) * ow + 2 * x] = e0; out[(2 * y) * ow + 2 * x + 1] = e1;
            out[(2 * y + 1) * ow + 2 * x] = e2; out[(2 * y + 1) * ow + 2 * x + 1] = e3;
        }
}

} // namespace

bool extract_cursor_frame(const CursorFrameSource& s, std::vector<std::uint32_t>& output) {
    output.clear();
    if (!s.pixels || !s.width || !s.height || (s.bits != 16 && s.bits != 32)) return false;
    const std::uint32_t bytes = s.bits / 8;
    output.resize(static_cast<std::size_t>(s.width) * s.height);
    for (std::uint32_t y = 0; y < s.height; ++y)
        for (std::uint32_t x = 0; x < s.width; ++x) {
            std::uint32_t raw = 0;
            std::memcpy(&raw, s.pixels + static_cast<std::size_t>(s.top + y) * s.pitch +
                                  static_cast<std::size_t>(s.left + x) * bytes, bytes);
            // The game's mask: a 16-bit pixel of 0, or 32-bit RGB of 0, is transparent.
            const bool transparent = s.bits == 16 ? (raw & 0xffffu) == 0 : (raw & 0x00ffffffu) == 0;
            output[static_cast<std::size_t>(y) * s.width + x] = transparent ? 0u :
                0xff000000u | (std::uint32_t(expand(raw, s.red_mask)) << 16) |
                (std::uint32_t(expand(raw, s.green_mask)) << 8) | expand(raw, s.blue_mask);
        }
    return true;
}

void upscale_cursor_frame(const std::vector<std::uint32_t>& input, std::uint32_t width,
    std::uint32_t height, float scale, std::vector<std::uint32_t>& output,
    std::uint32_t& output_width, std::uint32_t& output_height) {
    if (!(scale > 1.0f) || !width || !height) {
        output = input; output_width = width; output_height = height; return;
    }
    std::vector<std::uint32_t> big = input, next;
    std::uint32_t bw = width, bh = height;
    while (float(bw) < float(width) * scale) { epx(big, bw, bh, next); big.swap(next); bw *= 2; bh *= 2; }
    output_width = std::max(1u, static_cast<std::uint32_t>(std::lround(width * scale)));
    output_height = std::max(1u, static_cast<std::uint32_t>(std::lround(height * scale)));
    output.assign(static_cast<std::size_t>(output_width) * output_height, 0);
    const double fx = double(bw) / output_width, fy = double(bh) / output_height;
    for (std::uint32_t y = 0; y < output_height; ++y)
        for (std::uint32_t x = 0; x < output_width; ++x) {
            // Area average of the covered source footprint in premultiplied space.
            const double x0 = x * fx, x1 = x0 + fx, y0 = y * fy, y1 = y0 + fy;
            double sum[4]{}, area = 0;
            for (std::uint32_t sy = std::uint32_t(y0); sy < bh && sy < y1; ++sy) {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (std::uint32_t sx = std::uint32_t(x0); sx < bw && sx < x1; ++sx) {
                    const double wx = std::min<double>(sx + 1, x1) - std::max<double>(sx, x0);
                    const std::uint32_t p = big[static_cast<std::size_t>(sy) * bw + sx];
                    const double a = (p >> 24) / 255.0, weight = wx * wy;
                    sum[0] += ((p >> 16) & 0xff) * a * weight; sum[1] += ((p >> 8) & 0xff) * a * weight;
                    sum[2] += (p & 0xff) * a * weight; sum[3] += a * weight; area += weight;
                }
            }
            if (area <= 0 || sum[3] <= 0) continue;
            const double alpha = sum[3] / area;
            const auto channel = [&](double v) {
                return static_cast<std::uint32_t>(std::clamp(std::lround(v / sum[3]), 0L, 255L));
            };
            output[static_cast<std::size_t>(y) * output_width + x] =
                (static_cast<std::uint32_t>(std::clamp(std::lround(alpha * 255.0), 0L, 255L)) << 24) |
                (channel(sum[0]) << 16) | (channel(sum[1]) << 8) | channel(sum[2]);
        }
}

} // namespace owned
