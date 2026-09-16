#pragma once
#include <windows.h>
#include <cmath>
#include <cfloat>
#include <cstring>
// Algorithm copied from dynamic_lighting.cpp e2c4867; typed input replaces executable offsets.
namespace TerrainShadowPort {
static const float kTerrainClearanceQuantization=1024.0f;
static const float kTerrainClearanceMaximumDistance=16.0f;
static const float kTerrainClearanceSoftRadius=1.0f;
struct TerrainSelfShadowTopologyNode {
    DWORD z_bits;
    unsigned char occlusion;
    unsigned char valid;
    unsigned char endpoints_valid;
    unsigned char reserved;
    short clearance_q;
    unsigned short clearance_reserved;
    float normal_rgb[3];
    float shadow_rgb[3];
};

struct TerrainSelfShadowTopologyField {
    TerrainSelfShadowTopologyNode *nodes;
    unsigned char *surface_coverage;
    unsigned char *surface_valid;
    short *surface_clearance;
    DWORD node_count;
    int min_sector_x;
    int min_sector_y;
    int max_sector_x;
    int max_sector_y;
    int width;
    int height;
    int surface_width;
    int surface_height;
    float surface_step;
    float surface_min_u;
    float surface_min_v;
    float surface_unit_x;
    float surface_unit_y;
    float surface_perpendicular_x;
    float surface_perpendicular_y;
    LONG map_generation;
    DWORD sun_signature;
    DWORD valid_nodes;
    DWORD endpoint_nodes;
    unsigned long long captures;
    unsigned long long seam_duplicates;
    unsigned long long duplicate_z_disagreements;
    unsigned long long duplicate_visibility_disagreements;
    unsigned long long lookup_hits;
    unsigned long long lookup_misses;
    unsigned long long surface_rays;
    unsigned long long surface_tests;
    unsigned long long blocked_rays;
    unsigned long long fractional_nodes;
    unsigned long long native_lit_to_surface_shadow;
    unsigned long long native_shadow_to_surface_lit;
    unsigned long long surface_rebuilds;
    unsigned long long surface_rebuild_rejects;
    unsigned long long horizon_samples;
    unsigned long long horizon_rows;
    unsigned long long horizon_fractional_nodes;
    unsigned long long horizon_lit_to_shadow;
    unsigned long long horizon_shadow_to_lit;
    DWORD horizon_build_ms;
    bool horizon_sweep_ready;
    bool continuous_surface_ready;
    bool ready;
};

static bool TerrainSelfShadowTopologyHeightAt(
    const TerrainSelfShadowTopologyField *field,
    float x, float y, float *height) {
    if (!field || !field->nodes || !height ||
        !_finite(x) || !_finite(y) ||
        x < 0.0f || y < 0.0f ||
        x > (float)(field->width - 1) ||
        y > (float)(field->height - 1) ||
        field->width < 2 || field->height < 2)
        return false;

    int cell_x = (int)floorf(x);
    int cell_y = (int)floorf(y);
    float fx = x - (float)cell_x;
    float fy = y - (float)cell_y;
    if (cell_x >= field->width - 1) {
        cell_x = field->width - 2;
        fx = 1.0f;
    }
    if (cell_y >= field->height - 1) {
        cell_y = field->height - 2;
        fy = 1.0f;
    }
    const TerrainSelfShadowTopologyNode *n00 =
        &field->nodes[cell_y * field->width + cell_x];
    const TerrainSelfShadowTopologyNode *n10 = n00 + 1;
    const TerrainSelfShadowTopologyNode *n01 = n00 + field->width;
    const TerrainSelfShadowTopologyNode *n11 = n01 + 1;
    if (!n00->valid || !n10->valid || !n01->valid || !n11->valid)
        return false;
    float z00 = 0.0f, z10 = 0.0f, z01 = 0.0f, z11 = 0.0f;
    memcpy(&z00, &n00->z_bits, sizeof(z00));
    memcpy(&z10, &n10->z_bits, sizeof(z10));
    memcpy(&z01, &n01->z_bits, sizeof(z01));
    memcpy(&z11, &n11->z_bits, sizeof(z11));
    if (!_finite(z00) || !_finite(z10) ||
        !_finite(z01) || !_finite(z11))
        return false;

    // LOD 0 uses the v00-v11 diagonal in every terrain cell. Evaluate the
    // same two planar triangles instead of treating the heightfield as a
    // hollow volume or sampling one lattice vertex.
    if (fy <= fx) {
        *height = (1.0f - fx) * z00 +
            (fx - fy) * z10 + fy * z11;
    } else {
        *height = (1.0f - fy) * z00 +
            fx * z11 + (fy - fx) * z01;
    }
    return _finite(*height) != 0;
}

static short TerrainClearanceQuantize(float distance) {
    if (!_finite(distance)) distance = -kTerrainClearanceMaximumDistance;
    if (distance < -kTerrainClearanceMaximumDistance)
        distance = -kTerrainClearanceMaximumDistance;
    if (distance > kTerrainClearanceMaximumDistance)
        distance = kTerrainClearanceMaximumDistance;
    const float scaled = distance * kTerrainClearanceQuantization;
    return (short)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static float TerrainClearanceDecode(short value) {
    return (float)value / kTerrainClearanceQuantization;
}

static unsigned char TerrainClearanceCoverage(float distance) {
    float amount = (distance + kTerrainClearanceSoftRadius) /
                   (2.0f * kTerrainClearanceSoftRadius);
    if (amount <= 0.0f) return 0u;
    if (amount >= 1.0f) return 255u;
    amount = amount * amount * (3.0f - 2.0f * amount);
    return (unsigned char)(amount * 255.0f + 0.5f);
}

static bool Build(TerrainSelfShadowTopologyField *field, int add_x, int add_y, float rise_x, float rise_y) {
    if (!field || !field->nodes || field->valid_nodes != field->node_count) return false;
    const int abs_x = add_x < 0 ? -add_x : add_x;
    const int abs_y = add_y < 0 ? -add_y : add_y;
    const int dominant = abs_x > abs_y ? abs_x : abs_y;
    if (!dominant || !_finite(rise_x) || !_finite(rise_y)) {
        ++field->surface_rebuild_rejects;
        return false;
    }
    const DWORD started = GetTickCount();
    const float max_steps = (float)dominant;
    const float direction_x = (float)add_x / max_steps;
    const float direction_y = (float)add_y / max_steps;
    // 0.7.80 proved the unscaled slope too short and 0.7.79 proved a fourfold
    // range too long. Keep the accepted horizon generator fixed while testing
    // the binary-derived diagonal midpoint: half slope / twice the range.
    const float scale = 3.0f;
    const float rise_per_step = (abs_x >= abs_y && add_x
        ? rise_x * direction_x
        : rise_y * direction_y) / scale;
    const float direction_length = sqrtf(
        direction_x * direction_x + direction_y * direction_y);
    if (!_finite(rise_per_step) || rise_per_step < 0.0f ||
        !_finite(direction_length) || direction_length < 1.0e-5f) {
        ++field->surface_rebuild_rejects;
        return false;
    }
    const float unit_x = direction_x / direction_length;
    const float unit_y = direction_y / direction_length;
    const float perpendicular_x = -unit_y;
    const float perpendicular_y = unit_x;
    const float rise_per_world = rise_per_step / direction_length;
    const float max_distance = max_steps * direction_length;
    const float raster_step = 0.5f;

    const float corners_x[4] = {
        0.0f, (float)(field->width - 1), 0.0f,
        (float)(field->width - 1)
    };
    const float corners_y[4] = {
        0.0f, 0.0f, (float)(field->height - 1),
        (float)(field->height - 1)
    };
    float min_u = FLT_MAX, max_u = -FLT_MAX;
    float min_v = FLT_MAX, max_v = -FLT_MAX;
    for (int i = 0; i < 4; ++i) {
        const float u = corners_x[i] * unit_x + corners_y[i] * unit_y;
        const float v = corners_x[i] * perpendicular_x +
            corners_y[i] * perpendicular_y;
        if (u < min_u) min_u = u;
        if (u > max_u) max_u = u;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    min_u -= raster_step;
    min_v -= raster_step;
    max_u += raster_step;
    max_v += raster_step;
    const int raster_width = (int)ceilf((max_u - min_u) / raster_step) + 1;
    const int raster_height = (int)ceilf((max_v - min_v) / raster_step) + 1;
    const unsigned long long raster_count64 =
        (unsigned long long)raster_width * (unsigned long long)raster_height;
    if (raster_width < 3 || raster_height < 3 ||
        raster_count64 > 16000000ull) {
        ++field->surface_rebuild_rejects;
        return false;
    }
    const SIZE_T raster_count = (SIZE_T)raster_count64;
    float *heights = (float *)VirtualAlloc(NULL,
        raster_count * sizeof(float), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    float *clearance = (float *)VirtualAlloc(NULL,
        raster_count * sizeof(float), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *valid = (unsigned char *)VirtualAlloc(NULL,
        raster_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *coverage = (unsigned char *)VirtualAlloc(NULL,
        raster_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *filtered = (unsigned char *)VirtualAlloc(NULL,
        raster_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    int *deque = (int *)VirtualAlloc(NULL,
        (SIZE_T)raster_width * sizeof(int),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!heights || !clearance || !valid || !coverage || !filtered || !deque) {
        if (heights) VirtualFree(heights, 0, MEM_RELEASE);
        if (clearance) VirtualFree(clearance, 0, MEM_RELEASE);
        if (valid) VirtualFree(valid, 0, MEM_RELEASE);
        if (coverage) VirtualFree(coverage, 0, MEM_RELEASE);
        if (filtered) VirtualFree(filtered, 0, MEM_RELEASE);
        if (deque) VirtualFree(deque, 0, MEM_RELEASE);
        ++field->surface_rebuild_rejects;
        return false;
    }

    for (int row = 0; row < raster_height; ++row) {
        const float v = min_v + (float)row * raster_step;
        for (int column = 0; column < raster_width; ++column) {
            const float u = min_u + (float)column * raster_step;
            const float x = unit_x * u + perpendicular_x * v;
            const float y = unit_y * u + perpendicular_y * v;
            const SIZE_T index = (SIZE_T)row * raster_width + column;
            float height = 0.0f;
            if (TerrainSelfShadowTopologyHeightAt(field, x, y, &height)) {
                heights[index] = height;
                clearance[index] = -kTerrainClearanceMaximumDistance;
                valid[index] = 1;
                ++field->horizon_samples;
            }
        }
    }

    const int minimum_offset = 2;
    int maximum_offset = (int)floorf(max_distance / raster_step);
    if (maximum_offset < minimum_offset) maximum_offset = minimum_offset;
    if (maximum_offset >= raster_width) maximum_offset = raster_width - 1;
    for (int row = 0; row < raster_height; ++row) {
        int head = 0, tail = 0;
        const SIZE_T base = (SIZE_T)row * raster_width;
        for (int column = raster_width - 1; column >= 0; --column) {
            const int maximum_index = column + maximum_offset;
            while (head < tail && deque[head] > maximum_index) ++head;
            const int add_index = column + minimum_offset;
            if (add_index < raster_width && valid[base + add_index]) {
                const float add_u = min_u + (float)add_index * raster_step;
                const float add_value = heights[base + add_index] -
                    rise_per_world * add_u;
                while (head < tail) {
                    const int back = deque[tail - 1];
                    const float back_u = min_u + (float)back * raster_step;
                    const float back_value = heights[base + back] -
                        rise_per_world * back_u;
                    if (back_value > add_value) break;
                    --tail;
                }
                deque[tail++] = add_index;
            }
            if (!valid[base + column] || head >= tail) continue;
            const int blocker = deque[head];
            const float blocker_u = min_u + (float)blocker * raster_step;
            const float blocker_value = heights[base + blocker] -
                rise_per_world * blocker_u;
            const float receiver_u = min_u + (float)column * raster_step;
            const float receiver_value = heights[base + column] -
                rise_per_world * receiver_u;
            const float blocker_clearance =
                blocker_value - receiver_value - 0.015f;
            clearance[base + column] = blocker_clearance;
            if (blocker_clearance > 0.0f)
                coverage[base + column] = 255u;
        }
        ++field->horizon_rows;
    }

    // Convert vertical blocker excess into an approximate signed world-space
    // distance to its zero crossing. This preserves geometric motion when two
    // azimuth slices are interpolated; opacity is reconstructed only afterward.
    for (int row = 0; row < raster_height; ++row) {
        const SIZE_T base = (SIZE_T)row * raster_width;
        for (int column = 0; column < raster_width; ++column) {
            const SIZE_T index = base + column;
            if (!valid[index])
                continue;
            if (clearance[index] <= -kTerrainClearanceMaximumDistance) {
                heights[index] = -kTerrainClearanceMaximumDistance;
                continue;
            }
            const float center = clearance[index];
            float left_value = center, right_value = center;
            float top_value = center, bottom_value = center;
            if (column > 0 && valid[index - 1] &&
                clearance[index - 1] > -kTerrainClearanceMaximumDistance)
                left_value = clearance[index - 1];
            if (column + 1 < raster_width && valid[index + 1] &&
                clearance[index + 1] > -kTerrainClearanceMaximumDistance)
                right_value = clearance[index + 1];
            if (row > 0 && valid[index - raster_width] &&
                clearance[index - raster_width] >
                    -kTerrainClearanceMaximumDistance)
                top_value = clearance[index - raster_width];
            if (row + 1 < raster_height && valid[index + raster_width] &&
                clearance[index + raster_width] >
                    -kTerrainClearanceMaximumDistance)
                bottom_value = clearance[index + raster_width];
            const float gradient_x =
                (right_value - left_value) / (2.0f * raster_step);
            const float gradient_y =
                (bottom_value - top_value) / (2.0f * raster_step);
            float gradient = sqrtf(
                gradient_x * gradient_x + gradient_y * gradient_y);
            if (!_finite(gradient) || gradient < 0.015f) gradient = 0.015f;
            float distance = center / gradient;
            if (distance < -kTerrainClearanceMaximumDistance)
                distance = -kTerrainClearanceMaximumDistance;
            if (distance > kTerrainClearanceMaximumDistance)
                distance = kTerrainClearanceMaximumDistance;
            heights[index] = distance;
        }
    }

    // A separable five-tap binomial reconstruction is a one-world-unit
    // receiver footprint, not gap filling. It antialiases the binary horizon
    // crossing before the field is sampled back at terrain vertices.
    static const int kernel[5] = {1, 4, 6, 4, 1};
    for (int row = 0; row < raster_height; ++row) {
        const SIZE_T base = (SIZE_T)row * raster_width;
        for (int column = 0; column < raster_width; ++column) {
            if (!valid[base + column]) continue;
            int total = 0, weight = 0;
            for (int tap = -2; tap <= 2; ++tap) {
                const int sample = column + tap;
                if (sample < 0 || sample >= raster_width ||
                    !valid[base + sample]) continue;
                const int w = kernel[tap + 2];
                total += (int)coverage[base + sample] * w;
                weight += w;
            }
            filtered[base + column] = weight
                ? (unsigned char)((total + weight / 2) / weight) : 0u;
        }
    }
    for (int row = 0; row < raster_height; ++row) {
        const SIZE_T base = (SIZE_T)row * raster_width;
        for (int column = 0; column < raster_width; ++column) {
            if (!valid[base + column]) continue;
            int total = 0, weight = 0;
            for (int tap = -2; tap <= 2; ++tap) {
                const int sample = row + tap;
                if (sample < 0 || sample >= raster_height) continue;
                const SIZE_T sample_index =
                    (SIZE_T)sample * raster_width + column;
                if (!valid[sample_index]) continue;
                const int w = kernel[tap + 2];
                total += (int)filtered[sample_index] * w;
                weight += w;
            }
            coverage[base + column] = weight
                ? (unsigned char)((total + weight / 2) / weight) : 0u;
        }
    }

    for (int y = 0; y < field->height; ++y) {
        for (int x = 0; x < field->width; ++x) {
            TerrainSelfShadowTopologyNode *node =
                &field->nodes[y * field->width + x];
            if (!node->valid) continue;
            const unsigned char native_occlusion = node->occlusion;
            const float u = (float)x * unit_x + (float)y * unit_y;
            const float v = (float)x * perpendicular_x +
                (float)y * perpendicular_y;
            const float raster_x = (u - min_u) / raster_step;
            const float raster_y = (v - min_v) / raster_step;
            int left = (int)floorf(raster_x);
            int top = (int)floorf(raster_y);
            float fx = raster_x - (float)left;
            float fy = raster_y - (float)top;
            if (left < 0) { left = 0; fx = 0.0f; }
            if (top < 0) { top = 0; fy = 0.0f; }
            if (left >= raster_width - 1) {
                left = raster_width - 2; fx = 1.0f;
            }
            if (top >= raster_height - 1) {
                top = raster_height - 2; fy = 1.0f;
            }
            const float weights[4] = {
                (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                (1.0f - fx) * fy, fx * fy
            };
            const SIZE_T indices[4] = {
                (SIZE_T)top * raster_width + left,
                (SIZE_T)top * raster_width + left + 1,
                (SIZE_T)(top + 1) * raster_width + left,
                (SIZE_T)(top + 1) * raster_width + left + 1
            };
            float amount = 0.0f, clearance_amount = 0.0f, weight = 0.0f;
            for (int sample = 0; sample < 4; ++sample) {
                if (!valid[indices[sample]]) continue;
                amount += (float)coverage[indices[sample]] * weights[sample];
                clearance_amount += heights[indices[sample]] * weights[sample];
                weight += weights[sample];
            }
            const unsigned char occlusion = weight > 1.0e-6f
                ? (unsigned char)(amount / weight + 0.5f) : 0u;
            node->clearance_q = TerrainClearanceQuantize(
                weight > 1.0e-6f ? clearance_amount / weight :
                    -kTerrainClearanceMaximumDistance);
            node->occlusion = occlusion;
            if (occlusion > 0u && occlusion < 255u) {
                ++field->fractional_nodes;
                ++field->horizon_fractional_nodes;
            }
            if (native_occlusion < 128u && occlusion >= 128u) {
                ++field->native_lit_to_surface_shadow;
                ++field->horizon_lit_to_shadow;
            }
            if (native_occlusion >= 128u && occlusion < 128u) {
                ++field->native_shadow_to_surface_lit;
                ++field->horizon_shadow_to_lit;
            }
        }
    }
    // Direction-oriented sweep rasters cannot be interpolated across azimuths.
    // Resample every completed slice into the same fixed map XY grid first.
    const int world_width = (field->width - 1) * 2 + 1;
    const int world_height = (field->height - 1) * 2 + 1;
    const SIZE_T world_count = (SIZE_T)world_width * world_height;
    short *world_clearance = (short *)VirtualAlloc(NULL,
        world_count * sizeof(short), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *world_valid = (unsigned char *)VirtualAlloc(NULL,
        world_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char *world_coverage = (unsigned char *)VirtualAlloc(NULL,
        world_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!world_clearance || !world_valid || !world_coverage) {
        if (world_clearance) VirtualFree(world_clearance, 0, MEM_RELEASE);
        if (world_valid) VirtualFree(world_valid, 0, MEM_RELEASE);
        if (world_coverage) VirtualFree(world_coverage, 0, MEM_RELEASE);
        VirtualFree(deque, 0, MEM_RELEASE);
        VirtualFree(filtered, 0, MEM_RELEASE);
        VirtualFree(coverage, 0, MEM_RELEASE);
        VirtualFree(valid, 0, MEM_RELEASE);
        VirtualFree(clearance, 0, MEM_RELEASE);
        VirtualFree(heights, 0, MEM_RELEASE);
        ++field->surface_rebuild_rejects;
        return false;
    }
    for (int world_y = 0; world_y < world_height; ++world_y) {
        const float local_y = (float)world_y * raster_step;
        for (int world_x = 0; world_x < world_width; ++world_x) {
            const float local_x = (float)world_x * raster_step;
            const float u = local_x * unit_x + local_y * unit_y;
            const float v = local_x * perpendicular_x +
                local_y * perpendicular_y;
            const float sample_x = (u - min_u) / raster_step;
            const float sample_y = (v - min_v) / raster_step;
            int left = (int)floorf(sample_x);
            int top = (int)floorf(sample_y);
            if (left < 0 || top < 0 || left >= raster_width - 1 ||
                top >= raster_height - 1)
                continue;
            const float fx = sample_x - (float)left;
            const float fy = sample_y - (float)top;
            const float weights[4] = {
                (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                (1.0f - fx) * fy, fx * fy
            };
            const SIZE_T indices[4] = {
                (SIZE_T)top * raster_width + left,
                (SIZE_T)top * raster_width + left + 1,
                (SIZE_T)(top + 1) * raster_width + left,
                (SIZE_T)(top + 1) * raster_width + left + 1
            };
            float distance = 0.0f, weight = 0.0f;
            for (int sample = 0; sample < 4; ++sample) {
                if (!valid[indices[sample]]) continue;
                distance += heights[indices[sample]] * weights[sample];
                weight += weights[sample];
            }
            if (weight <= 1.0e-6f) continue;
            distance /= weight;
            const SIZE_T output = (SIZE_T)world_y * world_width + world_x;
            world_clearance[output] = TerrainClearanceQuantize(distance);
            world_coverage[output] = TerrainClearanceCoverage(distance);
            world_valid[output] = 1;
        }
    }
    VirtualFree(deque, 0, MEM_RELEASE);
    VirtualFree(filtered, 0, MEM_RELEASE);
    VirtualFree(coverage, 0, MEM_RELEASE);
    VirtualFree(valid, 0, MEM_RELEASE);
    field->surface_coverage = world_coverage;
    field->surface_valid = world_valid;
    field->surface_clearance = world_clearance;
    field->surface_width = world_width;
    field->surface_height = world_height;
    field->surface_step = raster_step;
    field->surface_min_u = 0.0f;
    field->surface_min_v = 0.0f;
    field->surface_unit_x = 1.0f;
    field->surface_unit_y = 0.0f;
    field->surface_perpendicular_x = 0.0f;
    field->surface_perpendicular_y = 1.0f;
    VirtualFree(clearance, 0, MEM_RELEASE);
    VirtualFree(heights, 0, MEM_RELEASE);
    field->horizon_build_ms = GetTickCount() - started;
    field->horizon_sweep_ready = true;
    field->continuous_surface_ready = true;
    ++field->surface_rebuilds;
    return true;
}

}
