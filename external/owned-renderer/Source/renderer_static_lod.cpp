#include "static_lod.h"

#include "owned_asset_loader.h"

#include "../ThirdParty/meshoptimizer/src/meshoptimizer.h"

#include <algorithm>
#include <cmath>

namespace owned {

namespace {

// Attribute weights against positions normalised to the mesh extent: shading
// normals and texture coordinates both hold the collapse back where they bend
// (GD3D11 uses the same 0.5 / 0.5 for its static LOD).
constexpr float kNormalWeight = 0.5f;
constexpr float kUvWeight = 0.5f;
// A level must drop at least this share of the previous level's triangles.
constexpr float kMinimumStep = 0.85f;

} // namespace

bool simplify_mesh_levels(const float* positions, std::size_t vertex_count, const float* attributes,
    const std::uint32_t* source, std::size_t index_count, float scale, NativeMeshLods& output, bool lock_border) noexcept try {
    output = {};
    if (!positions || !attributes || !source || !vertex_count || index_count % 3 || !index_count ||
        !(scale > 0) || !std::isfinite(scale))
        return false;
    std::vector<std::uint32_t> indices(source, source + index_count);
    for (const std::uint32_t index : indices) if (index >= vertex_count) return false;
    const float weights[5] = {kNormalWeight, kNormalWeight, kNormalWeight, kUvWeight, kUvWeight};
    std::vector<std::uint32_t> reduced(index_count);
    std::size_t previous = index_count;
    float previous_error = 0;
    for (std::uint32_t level = 0; level < kStaticLodMaxLevels; ++level) {
        const std::size_t target = (std::max)(std::size_t(3),
            std::size_t(double(index_count / 3) * kStaticLodLevelRatio[level]) * 3u);
        float error = 0;
        // Open borders stay (walls, planks, cards, atlas chart seams): a moved
        // border edge changes the silhouette. The error bound is open; the
        // renderer picks a level only where its error projects below its pixel limit.
        const std::size_t count = meshopt_simplifyWithAttributes(reduced.data(), indices.data(), index_count,
            positions, vertex_count, 12, attributes, 20, weights, 5, nullptr,
            target, 1.0f, lock_border ? meshopt_SimplifyLockBorder : 0u, &error);
        if (count < 3 || count % 3 || float(count) > float(previous) * kMinimumStep || !std::isfinite(error)) break;
        auto& stored = output.indices[level];
        stored.assign(reduced.begin(), reduced.begin() + std::ptrdiff_t(count));
        previous_error = (std::max)(previous_error, error * scale);
        output.error[level] = previous_error;
        output.levels = level + 1;
        previous = count;
    }
    return output.levels != 0;
} catch (...) {
    output = {};
    return false;
}

bool build_static_lods(const FigureCpuAsset& figure, NativeMeshLods& output) noexcept try {
    output = {};
    const std::size_t index_count = figure.indices.size();
    const std::size_t vertex_count = figure.components.size();
    if ((figure.shape_count != 1 && figure.shape_count != 8) || index_count % 3 || index_count / 3 < kStaticLodMinTriangles ||
        !vertex_count || figure.position_groups == 0 ||
        figure.positions.size() != std::size_t(figure.position_groups) * figure.shape_count)
        return false;
    const std::size_t position_count = std::size_t(figure.position_groups) * 4u;
    const std::size_t normal_count = figure.normals.size() * 4u;
    // Per component: the mean shape's position, then normal and UV.
    std::vector<float> positions(vertex_count * 3), shape_positions(vertex_count * 3), attributes(vertex_count * 5);
    float largest_scale = 0;
    for (std::uint32_t shape = 0; shape < figure.shape_count; ++shape) {
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            const auto& component = figure.components[vertex];
            if (component.position >= position_count || component.normal >= normal_count ||
                component.texture >= figure.texture_coordinates.size())
                return false;
            const auto& group = figure.positions[shape * figure.position_groups + (component.position >> 2)];
            const unsigned lane = component.position & 3u;
            const float value[3] = {group.x[lane], group.y[lane], group.z[lane]};
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(value[axis])) return false;
                shape_positions[vertex * 3 + axis] = value[axis];
                positions[vertex * 3 + axis] += value[axis] / float(figure.shape_count);
            }
        }
        largest_scale = (std::max)(largest_scale, meshopt_simplifyScale(shape_positions.data(), vertex_count, 12));
    }
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const auto& component = figure.components[vertex];
        const auto& group = figure.normals[component.normal >> 2];
        const unsigned lane = component.normal & 3u;
        float* attribute = &attributes[vertex * 5];
        attribute[0] = group.x[lane]; attribute[1] = group.y[lane]; attribute[2] = group.z[lane];
        attribute[3] = figure.texture_coordinates[component.texture].u;
        attribute[4] = figure.texture_coordinates[component.texture].v;
        for (unsigned k = 0; k < 5; ++k) if (!std::isfinite(attribute[k])) return false;
    }
    if (!(largest_scale > 0) || !std::isfinite(largest_scale)) return false;
    const std::vector<std::uint32_t> indices(figure.indices.begin(), figure.indices.end());
    return simplify_mesh_levels(positions.data(), vertex_count, attributes.data(), indices.data(), index_count,
                                largest_scale, output);
} catch (...) {
    output = {};
    return false;
}

} // namespace owned

namespace owned {

bool build_impostor_part(const FigureCpuAsset& figure, const NativePackedGroup* positions,
    const std::array<float, 3>& offset, NativeImpostorPart& output) noexcept try {
    output = {};
    const std::size_t index_count = figure.indices.size();
    const std::size_t vertex_count = figure.components.size();
    if (!positions || index_count % 3 || !index_count || !vertex_count || figure.position_groups == 0) return false;
    const std::size_t position_count = std::size_t(figure.position_groups) * 4u;
    const std::size_t normal_count = figure.normals.size() * 4u;
    output.vertices.assign(vertex_count * 8, 0.f);
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const auto& component = figure.components[vertex];
        if (component.position >= position_count || component.normal >= normal_count ||
            component.texture >= figure.texture_coordinates.size())
            return false;
        float* out = &output.vertices[vertex * 8];
        const auto& group = positions[component.position >> 2];
        const unsigned lane = component.position & 3u;
        out[0] = group.x[lane] + offset[0]; out[1] = group.y[lane] + offset[1]; out[2] = group.z[lane] + offset[2];
        const auto& normal = figure.normals[component.normal >> 2];
        const unsigned normal_lane = component.normal & 3u;
        out[3] = normal.x[normal_lane]; out[4] = normal.y[normal_lane]; out[5] = normal.z[normal_lane];
        out[6] = figure.texture_coordinates[component.texture].u;
        out[7] = figure.texture_coordinates[component.texture].v;
        for (unsigned k = 0; k < 8; ++k) if (!std::isfinite(out[k])) return false;
    }
    output.indices.assign(figure.indices.begin(), figure.indices.end());
    for (const std::uint32_t index : output.indices) if (index >= vertex_count) return false;
    return true;
} catch (...) {
    output = {};
    return false;
}

} // namespace owned
