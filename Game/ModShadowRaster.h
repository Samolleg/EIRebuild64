#pragma once
#include <windows.h>
#include <cstdint>
// Adapted from quiet e2c4867 shadow_fixes.cpp. Address hooks and telemetry excluded.
namespace ModShadow {
struct TreeShadowRasterVertex {
    LONG x;
    LONG y;
    LONG u;
    LONG v;
};

struct TreeShadowRasterTriangle {
    TreeShadowRasterVertex vertex[3];
    WORD *source;
    WORD *destination;
    LONG source_pitch_bytes;
    LONG destination_pitch_bytes;
};

static __forceinline int64_t TreeShadowEdge(LONG ax, LONG ay, LONG bx,
                                             LONG by, LONG px, LONG py) {
    return (int64_t)(px - ax) * (by - ay) -
           (int64_t)(py - ay) * (bx - ax);
}

static __forceinline DWORD TreeShadowCoverageHash(LONG x, LONG y) {
    DWORD value = (DWORD)x * 0x1F123BB5u ^ (DWORD)y * 0x05491333u;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

static const DWORD g_tree_shadow_alpha_density_percent = 75;
static const DWORD g_tree_shadow_alpha_dither_mode = 1;
static const bool g_tree_shadow_fast_raster = true;
static const bool g_tree_shadow_prebake_density = true;
static unsigned char g_tree_shadow_coverage[4096];
inline void InitCoverage() { static bool ready=false; if(ready)return; for(int y=0;y<64;++y)for(int x=0;x<64;++x)g_tree_shadow_coverage[y*64+x]=static_cast<unsigned char>(TreeShadowCoverageHash(x,y));ready=true; }
// e2c4867 preparation: thin partial masks once; preserve opaque fast path.
inline bool PrepareMask(WORD *pixels, unsigned side) {
    InitCoverage();
    bool opaque=true;for(unsigned i=0;i<side*side;i++)if(!(pixels[i]&0x8000)){opaque=false;break;}
    if(opaque)return true;
    for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++)
        if(g_tree_shadow_coverage[((y&63)<<6)|(x&63)] >= (75u*256u+99u)/100u) pixels[y*side+x]=0;
    return false;
}
static bool RasterTreeShadowAlpha(TreeShadowRasterTriangle *triangle) {
    InitCoverage();
    if (!triangle || !triangle->source || !triangle->destination)
        return false;

    const LONG source_width = triangle->source_pitch_bytes / 2;
    const LONG destination_width = triangle->destination_pitch_bytes / 2;
    if (source_width <= 0 || source_width > 4096 ||
        destination_width <= 0 || destination_width > 4096 ||
        (triangle->source_pitch_bytes & 1) != 0 ||
        (triangle->destination_pitch_bytes & 1) != 0)
        return false;

    LONG min_x = triangle->vertex[0].x;
    LONG max_x = triangle->vertex[0].x;
    LONG min_y = triangle->vertex[0].y;
    LONG max_y = triangle->vertex[0].y;
    for (int i = 1; i < 3; ++i) {
        if (triangle->vertex[i].x < min_x) min_x = triangle->vertex[i].x;
        if (triangle->vertex[i].x > max_x) max_x = triangle->vertex[i].x;
        if (triangle->vertex[i].y < min_y) min_y = triangle->vertex[i].y;
        if (triangle->vertex[i].y > max_y) max_y = triangle->vertex[i].y;
    }
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= destination_width) max_x = destination_width - 1;
    if (max_y >= destination_width) max_y = destination_width - 1;
    if (min_x > max_x || min_y > max_y) return true;

    int64_t area = TreeShadowEdge(
        triangle->vertex[0].x, triangle->vertex[0].y,
        triangle->vertex[1].x, triangle->vertex[1].y,
        triangle->vertex[2].x, triangle->vertex[2].y);
    if (area == 0) return true;
    const bool reverse = area < 0;
    if (reverse) area = -area;

    static const unsigned char kBayer4x4[16] = {
        0, 8, 2, 10,
        12, 4, 14, 6,
        3, 11, 1, 9,
        15, 7, 13, 5
    };
    const DWORD density_steps =
        (g_tree_shadow_alpha_density_percent * 16u + 99u) / 100u;
    const DWORD density_threshold =
        (g_tree_shadow_alpha_density_percent * 256u + 99u) / 100u;
    const LONG sign = reverse ? -1 : 1;
    const int64_t w0_dx = (int64_t)sign *
        (triangle->vertex[2].y - triangle->vertex[1].y);
    const int64_t w1_dx = (int64_t)sign *
        (triangle->vertex[0].y - triangle->vertex[2].y);
    const int64_t w2_dx = (int64_t)sign *
        (triangle->vertex[1].y - triangle->vertex[0].y);
    const int64_t w0_dy = (int64_t)sign *
        (triangle->vertex[1].x - triangle->vertex[2].x);
    const int64_t w1_dy = (int64_t)sign *
        (triangle->vertex[2].x - triangle->vertex[0].x);
    const int64_t w2_dy = (int64_t)sign *
        (triangle->vertex[0].x - triangle->vertex[1].x);
    int64_t row_w0 = sign * TreeShadowEdge(
        triangle->vertex[1].x, triangle->vertex[1].y,
        triangle->vertex[2].x, triangle->vertex[2].y, min_x, min_y);
    int64_t row_w1 = sign * TreeShadowEdge(
        triangle->vertex[2].x, triangle->vertex[2].y,
        triangle->vertex[0].x, triangle->vertex[0].y, min_x, min_y);
    int64_t row_w2 = sign * TreeShadowEdge(
        triangle->vertex[0].x, triangle->vertex[0].y,
        triangle->vertex[1].x, triangle->vertex[1].y, min_x, min_y);
    const double inverse_area = 1.0 / (double)area;
    int64_t row_u_sum = row_w0 * triangle->vertex[0].u +
                        row_w1 * triangle->vertex[1].u +
                        row_w2 * triangle->vertex[2].u;
    int64_t row_v_sum = row_w0 * triangle->vertex[0].v +
                        row_w1 * triangle->vertex[1].v +
                        row_w2 * triangle->vertex[2].v;
    const int64_t u_dx = w0_dx * triangle->vertex[0].u +
                         w1_dx * triangle->vertex[1].u +
                         w2_dx * triangle->vertex[2].u;
    const int64_t v_dx = w0_dx * triangle->vertex[0].v +
                         w1_dx * triangle->vertex[1].v +
                         w2_dx * triangle->vertex[2].v;
    const int64_t u_dy = w0_dy * triangle->vertex[0].u +
                         w1_dy * triangle->vertex[1].u +
                         w2_dy * triangle->vertex[2].u;
    const int64_t v_dy = w0_dy * triangle->vertex[0].v +
                         w1_dy * triangle->vertex[1].v +
                         w2_dy * triangle->vertex[2].v;
    // Span setup is paid per row. Narrow rows retain the original loop below.
    // This guard also bounds every edge/UV product and the direct UV jump in
    // int64_t, while preserving the original path for unusual descriptors.
    bool use_spans = max_x - min_x + 1 >= 16;
    if (use_spans) {
        for (int i = 0; i < 3; ++i) {
            const TreeShadowRasterVertex &vertex = triangle->vertex[i];
            if (vertex.x < -32767 || vertex.x > 32767 ||
                vertex.y < -32767 || vertex.y > 32767 ||
                vertex.u < -32767 || vertex.u > 32767 ||
                vertex.v < -32767 || vertex.v > 32767) {
                use_spans = false;
                break;
            }
        }
    }
    if (use_spans) {
        for (LONG y = min_y; y <= max_y; ++y) {
            LONG first = 0;
            LONG last = max_x - min_x;
            const int64_t row_edges[3] = { row_w0, row_w1, row_w2 };
            const int64_t edge_steps[3] = { w0_dx, w1_dx, w2_dx };
            for (int edge = 0; edge < 3; ++edge) {
                const int64_t w = row_edges[edge];
                const int64_t step = edge_steps[edge];
                if (step > 0 && w < 0) {
                    const int64_t bound = (-w + step - 1) / step;
                    if (bound > last) { first = last + 1; break; }
                    if (bound > first) first = (LONG)bound;
                } else if (step < 0) {
                    if (w < 0) { first = last + 1; break; }
                    const int64_t bound = w / (-step);
                    if (bound < last) last = (LONG)bound;
                } else if (step == 0 && w < 0) {
                    first = last + 1;
                    break;
                }
            }
            int64_t u_sum = row_u_sum + (int64_t)first * u_dx;
            int64_t v_sum = row_v_sum + (int64_t)first * v_dx;
            for (LONG x = min_x + first; x <= min_x + last;
                 ++x, u_sum += u_dx, v_sum += v_dx) {
                WORD *destination_pixel =
                    triangle->destination + (size_t)y * destination_width + x;
                LONG u = g_tree_shadow_fast_raster
                    ? (LONG)((double)u_sum * inverse_area)
                    : (LONG)(u_sum / area);
                LONG v = g_tree_shadow_fast_raster
                    ? (LONG)((double)v_sum * inverse_area)
                    : (LONG)(v_sum / area);
                if (u < 0 || v < 0 || u >= source_width || v >= source_width)
                    continue;
                WORD source = triangle->source[(size_t)v * source_width + u];
                if ((source & 0x8000u) == 0) continue;
                if (!g_tree_shadow_prebake_density &&
                    g_tree_shadow_alpha_density_percent < 100u) {
                    // Anchor thinning to the caster texture rather than the
                    // 64x64 destination silhouette.  Destination coordinates can
                    // shift when camera/LOD-dependent projection bounds are
                    // rebuilt, which made otherwise static foliage shimmer.
                    // Source UVs identify the same foliage texel across rebuilds.
                    const LONG coverage_x = u;
                    const LONG coverage_y = v;
                    if (g_tree_shadow_alpha_dither_mode == 0u) {
                        if (kBayer4x4[((coverage_y & 3) << 2) |
                                      (coverage_x & 3)] >=
                            density_steps)
                            continue;
                    } else if (g_tree_shadow_coverage[
                                   ((coverage_y & 63) << 6) |
                                   (coverage_x & 63)] >=
                               density_threshold) {
                        continue;
                    }
                }
                *destination_pixel = 0xF000u;
            }
            row_w0 += w0_dy;
            row_w1 += w1_dy;
            row_w2 += w2_dy;
            row_u_sum += u_dy;
            row_v_sum += v_dy;
        }
        return true;
    }

    for (LONG y = min_y; y <= max_y; ++y) {
        int64_t w0 = row_w0;
        int64_t w1 = row_w1;
        int64_t w2 = row_w2;
        int64_t u_sum = row_u_sum;
        int64_t v_sum = row_v_sum;
        for (LONG x = min_x; x <= max_x;
             ++x, w0 += w0_dx, w1 += w1_dx, w2 += w2_dx,
             u_sum += u_dx, v_sum += v_dx) {
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            WORD *destination_pixel =
                triangle->destination + (size_t)y * destination_width + x;
            LONG u = g_tree_shadow_fast_raster
                ? (LONG)((double)u_sum * inverse_area)
                : (LONG)(u_sum / area);
            LONG v = g_tree_shadow_fast_raster
                ? (LONG)((double)v_sum * inverse_area)
                : (LONG)(v_sum / area);
            if (u < 0 || v < 0 || u >= source_width || v >= source_width)
                continue;
            WORD source = triangle->source[(size_t)v * source_width + u];
            if ((source & 0x8000u) == 0) continue;
            if (!g_tree_shadow_prebake_density &&
                g_tree_shadow_alpha_density_percent < 100u) {
                // Anchor thinning to the caster texture rather than the
                // 64x64 destination silhouette.  Destination coordinates can
                // shift when camera/LOD-dependent projection bounds are
                // rebuilt, which made otherwise static foliage shimmer.
                // Source UVs identify the same foliage texel across rebuilds.
                const LONG coverage_x = u;
                const LONG coverage_y = v;
                if (g_tree_shadow_alpha_dither_mode == 0u) {
                    if (kBayer4x4[((coverage_y & 3) << 2) |
                                  (coverage_x & 3)] >=
                        density_steps)
                        continue;
                } else if (g_tree_shadow_coverage[
                               ((coverage_y & 63) << 6) |
                               (coverage_x & 63)] >=
                           density_threshold) {
                    continue;
                }
            }
            *destination_pixel = 0xF000u;
        }
        row_w0 += w0_dy;
        row_w1 += w1_dy;
        row_w2 += w2_dy;
        row_u_sum += u_dy;
        row_v_sum += v_dy;
    }
    return true;
}

}
