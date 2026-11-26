#include "retained_static_submission.h"
#include "astral_rift.h"
#include <atomic>
#include <map>
#include <mutex>
#include "fast_float.h"
#include "shadow_foliage.h"
#include "visual_highlight.h"
#include "contact_blend.h"
#include "native_instance_flags.h"
#include "fur.h"
#include "figure_service_nodes.h"
#include "skn_material_view.h"
#include "gltf_asset.h"
#include "gltf_weapon_grip.h"
#include "environment_material.h"
#include "environment_asset.h"
#include "world_scene_algorithms.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <d3d.h>

namespace owned {
namespace {

constexpr std::uint32_t kFigureSignature1 = 0x31474946u;

std::array<float, 4> retained_visual_emissive(
    const VisualTickValues& values, bool quest_light_active,
    const MaterialCpuAsset& material) noexcept {
    HighlightMaterial stack;
    stack.emissive = material.emissive;
    stack.previous = material.previous_highlight_emissive;
    stack.oldest = material.oldest_highlight_emissive;
    stack.has_previous = material.has_previous_highlight_emissive;
    stack.has_oldest = material.has_oldest_highlight_emissive;
    std::array<float, 4> shown = visual_highlight_emissive(values.object_flags, values.unit,
        values.timed_highlight_active, quest_light_active, values.highlight, stack);
    // Ray traced lighting takes the authored emissive, never a highlight.
    const std::array<float, 4> authored = visual_authored_emissive(stack);
    if (shown[0] != authored[0] || shown[1] != authored[1] || shown[2] != authored[2])
        shown[3] = visual_pack_authored_emissive(authored);
    else if (shown[3] < 0.0f) shown[3] = 0.0f;
    return shown;
}

bool retained_visual_selection_envelope_radius(
    const RetainedVisualSample& sample, float& radius) noexcept;

std::uint64_t hash_bytes(std::uint64_t hash, const void* bytes,
                         std::size_t count) noexcept {
    const auto* current = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t index = 0; index < count; ++index) {
        hash ^= current[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t hash_string(std::uint64_t hash,
                          const std::string& value) noexcept {
    return hash_bytes(hash, value.data(), value.size());
}

float blend(float first, float second, float amount) noexcept {
    return first + (second - first) * amount;
}

float select_endpoint(const float* values,
                      const std::array<float, 3>& selector) noexcept {
    const float short_shape = blend(blend(values[0], values[1], selector[0]),
                              blend(values[2], values[3], selector[0]),
                              selector[1]);
    const float tall = blend(blend(values[4], values[5], selector[0]),
                             blend(values[6], values[7], selector[0]),
                             selector[1]);
    return blend(short_shape, tall, selector[2]);
}

std::array<float, 3> selected_bone(
    const ModelNodeCpuAsset& node,
    const std::array<float, 3>& selector) noexcept {
    if (node.bone_positions.size() == 3u)
        return {{node.bone_positions[0], node.bone_positions[1], node.bone_positions[2]}};
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        float endpoints[8]{};
        for (std::size_t shape = 0; shape < 8; ++shape)
            endpoints[shape] = node.bone_positions[shape * 3u + axis];
        result[axis] = select_endpoint(endpoints, selector);
    }
    return result;
}

FigureBounds selected_bounds(const FigureCpuAsset& figure,
                             const std::array<float, 3>& selector) noexcept {
    if (figure.shape_count == 1u) return figure.bounds.front();
    FigureBounds result{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        float center[8]{}, minimum[8]{}, maximum[8]{};
        for (std::size_t shape = 0; shape < 8; ++shape) {
            center[shape] = figure.bounds[shape].center[axis];
            minimum[shape] = figure.bounds[shape].minimum[axis];
            maximum[shape] = figure.bounds[shape].maximum[axis];
        }
        result.center[axis] = select_endpoint(center, selector);
        result.minimum[axis] = select_endpoint(minimum, selector);
        result.maximum[axis] = select_endpoint(maximum, selector);
    }
    float radius[8]{};
    for (std::size_t shape = 0; shape < 8; ++shape)
        radius[shape] = figure.bounds[shape].radius;
    result.radius = select_endpoint(radius, selector);
    // The shipped FIG8 path accepts raw complection extrapolation. That can
    // reverse a box endpoint pair or the sign of the radius even when every
    // authored endpoint is conventional. Legacy visibility consumes both box
    // endpoints and both +/- radius corners, so ordering/sign do not change
    // the tested volume. Normalize that equivalent volume for the modern AABB
    // contract without clamping or otherwise changing the selected geometry.
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (result.minimum[axis] > result.maximum[axis])
            std::swap(result.minimum[axis], result.maximum[axis]);
    result.radius = std::fabs(result.radius);
    return result;
}

bool selected_groups(const std::vector<NativePackedGroup>& endpoints,
                     std::uint32_t groups, std::uint32_t shapes,
                     const std::array<float, 3>& selector,
                     std::vector<NativePackedGroup>& output) {
    if (groups == 0 || (shapes != 1u && shapes != 8u) || endpoints.size() !=
            static_cast<std::size_t>(groups) * shapes)
        return false;
    output.resize(groups);
    for (std::size_t group = 0; group < output.size(); ++group) {
        NativePackedGroup& selected = output[group];
        for (std::size_t lane = 0; lane < 4; ++lane) {
            float x[8]{}, y[8]{}, z[8]{}, w[8]{};
            for (std::size_t shape = 0; shape < shapes; ++shape) {
                const NativePackedGroup& endpoint =
                    endpoints[shape * groups + group];
                x[shape] = endpoint.x[lane];
                y[shape] = endpoint.y[lane];
                z[shape] = endpoint.z[lane];
                w[shape] = endpoint.w[lane];
            }
            selected.x[lane] = shapes == 1u ? x[0] : select_endpoint(x, selector);
            selected.y[lane] = shapes == 1u ? y[0] : select_endpoint(y, selector);
            selected.z[lane] = shapes == 1u ? z[0] : select_endpoint(z, selector);
            selected.w[lane] = shapes == 1u ? w[0] : select_endpoint(w, selector);
            if (!finite_value(selected.x[lane]) ||
                !finite_value(selected.y[lane]) ||
                !finite_value(selected.z[lane]) ||
                !finite_value(selected.w[lane]))
                return false;
        }
    }
    return true;
}

NativeOwnedTextureFormat texture_format(MmpFormat format) noexcept {
    switch (format) {
    case MmpFormat::dxt1: return NativeOwnedTextureFormat::dxt1;
    case MmpFormat::dxt3: return NativeOwnedTextureFormat::dxt3;
    case MmpFormat::dxt5: return NativeOwnedTextureFormat::dxt5;
    case MmpFormat::rgb565: return NativeOwnedTextureFormat::rgb565;
    case MmpFormat::argb1555: return NativeOwnedTextureFormat::argb1555;
    case MmpFormat::argb4444: return NativeOwnedTextureFormat::argb4444;
    case MmpFormat::argb8888: return NativeOwnedTextureFormat::argb8888;
    default: return NativeOwnedTextureFormat::dxt1;
    }
}

std::uintptr_t folded_identity(std::uint64_t value) noexcept {
    if constexpr (sizeof(std::uintptr_t) < sizeof(value))
        value ^= value >> 32u;
    const auto result = static_cast<std::uintptr_t>(value);
    return result ? result : 1u;
}

bool prepare_failed(RetainedStaticPrepareDiagnostic* diagnostic,
                    RetainedStaticPrepareResult result,
                    std::size_t binding_index =
                        kRetainedStaticPrepareNoIndex,
                    std::size_t node_index =
                        kRetainedStaticPrepareNoIndex,
                    std::int32_t signed_id = 0) noexcept {
    if (diagnostic) {
        diagnostic->result = result;
        diagnostic->binding_index = binding_index;
        diagnostic->node_index = node_index;
        diagnostic->signed_id = signed_id;
    }
    return false;
}

} // namespace

NativeWorldLighting shadow_projection_lighting(const NativeWorldLighting& lighting) noexcept {
    NativeWorldLighting result=lighting;
    double length_squared=0;
    for(unsigned axis=0;axis<3;++axis)
        length_squared+=double(lighting.sunlight_direction[axis])*
            lighting.sunlight_direction[axis];
    // Direct sunlight keeps the game's exact direction. Shadow projections
    // remain fixed through the smaller rotations between adjacent world ticks.
    if (!(length_squared>1e-12) || !finite_value(length_squared)) return result;
    // The 0.5-degree step visibly stepped time-of-day shadows without buying
    // reuse; keep the original fine per-axis snap (~0.1 degree).
    constexpr double steps=512;
    const double scale=steps/std::sqrt(length_squared);
    std::array<double,3> snapped{};
    double snapped_length_squared=0;
    for(unsigned axis=0;axis<3;++axis) {
        snapped[axis]=std::round(double(lighting.sunlight_direction[axis])*scale)/steps;
        snapped_length_squared+=snapped[axis]*snapped[axis];
    }
    const double snapped_length=std::sqrt(snapped_length_squared);
    for(unsigned axis=0;axis<3;++axis)
        result.sunlight_direction[axis]=static_cast<float>(snapped[axis]/snapped_length);
    return result;
}

bool retained_object_matrix(
    const std::array<float, 3>& position,
    const std::array<float, 4>& quaternion,
    std::array<float, 16>& matrix) noexcept {
    const float w = quaternion[0];
    const float x = quaternion[1];
    const float y = quaternion[2];
    const float z = quaternion[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    matrix = {{
        1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz),
        2.0f * (xz + wy), position[0],
        2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz),
        2.0f * (yz - wx), position[1],
        2.0f * (xz - wy), 2.0f * (yz + wx),
        1.0f - 2.0f * (xx + yy), position[2],
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    return std::all_of(matrix.begin(), matrix.end(),
                       [](float value) { return finite_value(value); });
}

const char* retained_static_prepare_result_name(
    RetainedStaticPrepareResult result) noexcept {
    switch (result) {
    case RetainedStaticPrepareResult::not_attempted: return "not_attempted";
    case RetainedStaticPrepareResult::success: return "success";
    case RetainedStaticPrepareResult::source_collision: return "source_collision";
    case RetainedStaticPrepareResult::missing_terrain: return "missing_terrain";
    case RetainedStaticPrepareResult::binding_identity: return "binding_identity";
    case RetainedStaticPrepareResult::source_values: return "source_values";
    case RetainedStaticPrepareResult::model_lookup: return "model_lookup";
    case RetainedStaticPrepareResult::model_structure: return "model_structure";
    case RetainedStaticPrepareResult::terrain_sample: return "terrain_sample";
    case RetainedStaticPrepareResult::grounded_position: return "grounded_position";
    case RetainedStaticPrepareResult::object_transform: return "object_transform";
    case RetainedStaticPrepareResult::figure_structure: return "figure_structure";
    case RetainedStaticPrepareResult::component_range: return "component_range";
    case RetainedStaticPrepareResult::index_range: return "index_range";
    case RetainedStaticPrepareResult::bone_translation: return "bone_translation";
    case RetainedStaticPrepareResult::parent_order: return "parent_order";
    case RetainedStaticPrepareResult::node_bounds: return "node_bounds";
    case RetainedStaticPrepareResult::position_selection: return "position_selection";
    case RetainedStaticPrepareResult::texture_selector: return "texture_selector";
    case RetainedStaticPrepareResult::texture_lookup: return "texture_lookup";
    case RetainedStaticPrepareResult::exception: return "exception";
    }
    return "unknown";
}

bool retained_static_node_selected(const StaticObjectSource& source,
    const ModelCpuAsset& model, std::size_t index) noexcept {
    if (source.part_names.empty()) return true;
    if (index >= model.nodes.size()) return false;
    const std::string& name = model.nodes[index].name;
    return std::any_of(source.part_names.begin(), source.part_names.end(),
        [&](const std::string& part) {
            return part.size() == name.size() && std::equal(part.begin(), part.end(),
                name.begin(), [](char folded, char raw) {
                    return folded == ((raw >= 'A' && raw <= 'Z') ? char(raw + 32) : raw);
                });
        });
}

const char* retained_static_structure_omission(
    const StaticObjectSource& source,
    const OwnedMapSnapshot& snapshot) noexcept {
    if(snapshot.environment.count(source.visual_reference))return nullptr;
    const auto model = snapshot.models.find(source.visual_reference);
    if (model == snapshot.models.end() || !model->second) return "model_lookup";
    if (model->second->nodes.empty() ||
        model->second->links.size() != model->second->nodes.size())
        return "model_structure";
    for (const float selector : source.shape)
        if (!finite_value(selector)) return "source_values";
    const auto texture_supported = [&](std::uint32_t type_id) {
        const std::string* name = figure_texture_for_type(source, type_id);
        if (!name) return false;
        const auto found = snapshot.textures.find(*name);
        if (found == snapshot.textures.end() || !found->second ||
            !found->second->source || found->second->width == 0 ||
            found->second->height == 0 || found->second->mips.empty())
            return false;
        const TextureCpuAsset& texture = *found->second;
        return std::all_of(
            texture.mips.begin(), texture.mips.end(),
            [&](const MmpMipLevel& mip) {
                return mip.width != 0 && mip.height != 0 && mip.bytes != 0 &&
                    mip.byte_offset <= texture.source->size() &&
                    mip.bytes <= texture.source->size() - mip.byte_offset;
            });
    };
    bool drawn = false;
    for (std::size_t index = 0; index < model->second->nodes.size(); ++index) {
        const ModelNodeCpuAsset& node = model->second->nodes[index];
        const ModelLink& link = model->second->links[index];
        if (link.parent_index != UINT_MAX && link.parent_index >= index)
            return "parent_order";
        if (node.bone_positions.size() != 3u && node.bone_positions.size() != 24u)
            return "figure_structure";
        if (!retained_static_node_selected(source, *model->second, index)) continue;
        drawn = true;
        if (node.figure.signature != (node.figure.shape_count == 1u ? kFigureSignature1 : kFigureSignature8) ||
            (node.figure.shape_count != 1u && node.figure.shape_count != 8u) ||
            node.figure.position_groups == 0 ||
            node.figure.positions.size() !=
                static_cast<std::size_t>(node.figure.position_groups) * node.figure.shape_count ||
            node.figure.normal_groups == 0 ||
            node.figure.normals.size() !=
                static_cast<std::size_t>(node.figure.normal_groups) ||
            node.figure.bounds.size() != node.figure.shape_count ||
            node.figure.positions.empty() || node.figure.normals.empty() ||
            node.figure.components.empty() || node.figure.indices.empty() ||
            node.figure.texture_coordinates.empty() ||
            node.figure.indices.size() % 3u != 0u)
            return "figure_structure";
        if (!texture_supported(node.figure.texture_number)) return "texture_lookup";
        const std::size_t position_count =
            static_cast<std::size_t>(node.figure.position_groups) * 4u;
        const std::size_t normal_count =
            static_cast<std::size_t>(node.figure.normal_groups) * 4u;
        for (const NativeVertexComponent& component : node.figure.components)
            if (component.position >= position_count ||
                component.normal >= normal_count ||
                component.texture >= node.figure.texture_coordinates.size())
                return "component_range";
        for (const std::uint16_t component : node.figure.indices)
            if (component >= node.figure.components.size()) return "index_range";
    }
    return drawn ? nullptr : "part_lookup";
}

bool retained_static_source_eligible(
    const StaticObjectSource& source,
    const OwnedMapSnapshot& snapshot) noexcept {
    if (source.rejection != StaticObjectRejection::none || source.nid == 0 ||
        source.nid > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)()) ||
        (source.family != 53 && source.family != 55 && !(source.family>=53&&source.family<=56&&snapshot.environment.count(source.visual_reference))) || source.quest != 0 ||
        !source.unknown_field_ids.empty())
        return false;
    return !retained_static_structure_omission(source, snapshot);
}

std::array<float, 16> retained_multiply_matrix(
    const std::array<float, 16>& left,
    const std::array<float, 16>& right) noexcept {
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t lane = 0; lane < 4; ++lane)
                result[row * 4 + column] +=
                    left[row * 4 + lane] * right[lane * 4 + column];
    return result;
}

std::array<float, 16> retained_transpose_matrix(
    const std::array<float, 16>& source) noexcept {
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            result[row * 4 + column] = source[column * 4 + row];
    return result;
}

bool retained_bounds_visible(
    const FigureBounds& bounds,
    const std::array<float, 16>& object_to_clip) noexcept {
    std::array<float, 3> minimum{}, maximum{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        minimum[axis] = bounds.center[axis] + bounds.minimum[axis];
        maximum[axis] = bounds.center[axis] + bounds.maximum[axis];
        if (!finite_value(minimum[axis]) ||
            !finite_value(maximum[axis]) || minimum[axis] > maximum[axis])
            return false;
    }
    std::uint32_t outside = 0x3fu;
    for (std::uint32_t corner = 0; corner < 8u; ++corner) {
        const float x = (corner & 1u) ? maximum[0] : minimum[0];
        const float y = (corner & 2u) ? maximum[1] : minimum[1];
        const float z = (corner & 4u) ? maximum[2] : minimum[2];
        const float clip_x = object_to_clip[0] * x +
            object_to_clip[1] * y + object_to_clip[2] * z + object_to_clip[3];
        const float clip_y = object_to_clip[4] * x +
            object_to_clip[5] * y + object_to_clip[6] * z + object_to_clip[7];
        const float clip_z = object_to_clip[8] * x +
            object_to_clip[9] * y + object_to_clip[10] * z + object_to_clip[11];
        const float clip_w = object_to_clip[12] * x +
            object_to_clip[13] * y + object_to_clip[14] * z + object_to_clip[15];
        if (!finite_value(clip_x) || !finite_value(clip_y) ||
            !finite_value(clip_z) || !finite_value(clip_w))
            return false;
        std::uint32_t mask = 0;
        if (clip_x < -clip_w) mask |= 1u;
        if (clip_x > clip_w) mask |= 2u;
        if (clip_y < -clip_w) mask |= 4u;
        if (clip_y > clip_w) mask |= 8u;
        if (clip_z < 0.0f) mask |= 16u;
        if (clip_z > clip_w) mask |= 32u;
        outside &= mask;
    }
    return outside == 0;
}

bool prepare_retained_static_candidate(
    std::shared_ptr<const OwnedMapSnapshot> snapshot,
    std::shared_ptr<const TerrainCpuAsset> terrain,
    const StaticObjectSource& object,
    std::shared_ptr<RetainedStaticSourceCandidate>& prepared,
    RetainedStaticPrepareDiagnostic* diagnostic,
    std::size_t source_index) noexcept {
    prepared.reset();
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->result = RetainedStaticPrepareResult::success;
        diagnostic->source_bindings = 1;
        diagnostic->binding_index = source_index;
        diagnostic->signed_id = object.nid <= static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)())
            ? static_cast<std::int32_t>(object.nid) : 0;
    }
    const std::int32_t signed_id = diagnostic ? diagnostic->signed_id :
        (object.nid <= static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)())
            ? static_cast<std::int32_t>(object.nid) : 0);
    // Publication has already classified source-family eligibility. From this
    // point onward validate and compile the expensive immutable asset graph
    // exactly once.
    if (!snapshot || signed_id <= 0)
        return prepare_failed(diagnostic,
                              RetainedStaticPrepareResult::source_values,
                              source_index, kRetainedStaticPrepareNoIndex,
                              signed_id);
    std::size_t current_node = kRetainedStaticPrepareNoIndex;
    try {
        auto candidate = std::make_shared<RetainedStaticSourceCandidate>();
        candidate->nid = object.nid;
        candidate->family = object.family;
        candidate->signed_id = signed_id;
        candidate->snapshot = std::move(snapshot);
        candidate->source = &object;
        prepared = candidate;
        if (!terrain)
            return prepare_failed(diagnostic,
                                  RetainedStaticPrepareResult::missing_terrain,
                                  source_index, kRetainedStaticPrepareNoIndex,
                                  signed_id);

        const auto finite = [](float value) { return finite_value(value); };
        if (!std::all_of(object.position.begin(), object.position.end(), finite) ||
            !std::all_of(object.quaternion.begin(), object.quaternion.end(), finite) ||
            !std::all_of(object.shape.begin(), object.shape.end(), finite))
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::source_values,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);

        const auto environment=candidate->snapshot->environment.find(object.visual_reference);
        if(environment!=candidate->snapshot->environment.end()) {
            auto assembly=std::make_shared<VisualAssemblyAsset>(*environment->second);
            environment::reshape(*assembly,object.shape);
            if(!object.part_names.empty()) {
                assembly->parts.erase(std::remove_if(assembly->parts.begin(),assembly->parts.end(),[&](const auto& p){
                    return std::none_of(object.part_names.begin(),object.part_names.end(),[&](const auto& n){return n==p.name||n==p.name.substr(0,p.name.find('#'));});}),assembly->parts.end());
                if(assembly->parts.empty())return prepare_failed(diagnostic,RetainedStaticPrepareResult::figure_structure,source_index,0,signed_id);
            }
            StaticObjectSource prototype=object;ActiveStaticsModelRequest request{0,assembly,object.shape};
            auto snapshotOwner=candidate->snapshot;
            if(!prepare_live_static_candidate(request,prototype,signed_id,candidate))return prepare_failed(diagnostic,RetainedStaticPrepareResult::figure_structure,source_index,0,signed_id);
            candidate->snapshot=std::move(snapshotOwner);candidate->source=&object;candidate->nid=object.nid;candidate->family=object.family;
            float ground=0;if(!sample_terrain_height(*terrain,object.position[0],object.position[1],ground))return prepare_failed(diagnostic,RetainedStaticPrepareResult::terrain_sample,source_index,0,signed_id);
            candidate->world_position=object.position;candidate->world_position[2]+=ground;
            std::array<float,16> placement{};if(!retained_object_matrix(candidate->world_position,object.quaternion,placement))return false;
            for(auto& node:candidate->nodes)node.object_to_world=retained_multiply_matrix(placement,node.object_local);
            const auto root=candidate->root_world;
            for(unsigned a=0;a<3;++a)candidate->root_world[a]=placement[a*4]*root[0]+placement[a*4+1]*root[1]+placement[a*4+2]*root[2]+placement[a*4+3];
            prepared=candidate;return true;
        }
        const auto model_found = candidate->snapshot->models.find(
            object.visual_reference);
        if (model_found == candidate->snapshot->models.end() ||
            !model_found->second)
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::model_lookup,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);
        const ModelCpuAsset& model = *model_found->second;
        if (model.links.size() != model.nodes.size() || model.nodes.empty())
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::model_structure,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);

        auto assembly = std::make_shared<VisualAssemblyAsset>();
        assembly->revision = 1; // Constant within this immutable source value.
        assembly->skeleton = model_found->second;
        assembly->shape = object.shape;
        assembly->parts.reserve(model.nodes.size());
        auto material = std::make_shared<MaterialCpuAsset>();
        // Preserve the established rigid source material used by both spans.
        material->diffuse = {1, 1, 1, 1};

        float ground_height = 0.0f;
        if (!sample_terrain_height(*terrain, object.position[0],
                                   object.position[1], ground_height) ||
            !finite_value(ground_height))
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::terrain_sample,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);
        candidate->world_position = object.position;
        candidate->world_position[2] += ground_height;
        if (!finite_value(candidate->world_position[2]))
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::grounded_position,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);

        candidate->nodes.reserve(model.nodes.size());
        std::vector<std::array<float, 3>> accumulated(model.nodes.size());
        std::array<float, 16> placement{};
        if (!retained_object_matrix(candidate->world_position,
                                    object.quaternion, placement))
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::object_transform,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);
        for (std::size_t index = 0; index < model.nodes.size(); ++index) {
            current_node = index;
            const ModelNodeCpuAsset& source_node = model.nodes[index];
            const ModelLink& link = model.links[index];
            const FigureCpuAsset& figure = source_node.figure;
            // A node outside the MOB part list contributes only its offset.
            const bool drawn = retained_static_node_selected(object, model, index);
            if ((source_node.bone_positions.size() != 3u && source_node.bone_positions.size() != 24u) ||
                (drawn && (figure.signature != (figure.shape_count == 1u ? kFigureSignature1 : kFigureSignature8) ||
                (figure.shape_count != 1u && figure.shape_count != 8u) || figure.position_groups == 0 ||
                figure.normal_groups == 0 ||
                figure.positions.size() !=
                    static_cast<std::size_t>(figure.position_groups) * figure.shape_count ||
                figure.normals.size() !=
                    static_cast<std::size_t>(figure.normal_groups) ||
                figure.bounds.size() != figure.shape_count ||
                figure.texture_coordinates.empty() ||
                figure.components.empty() || figure.indices.empty() ||
                figure.indices.size() % 3u != 0u)))
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::figure_structure,
                    source_index, index, signed_id);

            const std::size_t position_count =
                static_cast<std::size_t>(figure.position_groups) * 4u;
            const std::size_t normal_count =
                static_cast<std::size_t>(figure.normal_groups) * 4u;
            if (drawn) for (const NativeVertexComponent& component : figure.components)
                if (component.position >= position_count ||
                    component.normal >= normal_count ||
                    component.texture >= figure.texture_coordinates.size())
                    return prepare_failed(
                        diagnostic, RetainedStaticPrepareResult::component_range,
                        source_index, index, signed_id);
            if (drawn) for (const std::uint16_t component : figure.indices)
                if (component >= figure.components.size())
                    return prepare_failed(
                        diagnostic, RetainedStaticPrepareResult::index_range,
                        source_index, index, signed_id);

            std::array<float, 3> translation =
                selected_bone(source_node, object.shape);
            if (!std::all_of(translation.begin(), translation.end(), finite))
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::bone_translation,
                    source_index, index, signed_id);
            if (link.parent_index != UINT_MAX) {
                if (link.parent_index >= index)
                    return prepare_failed(
                        diagnostic, RetainedStaticPrepareResult::parent_order,
                        source_index, index, signed_id);
                for (std::size_t axis = 0; axis < 3; ++axis)
                    translation[axis] += accumulated[link.parent_index][axis];
            }
            accumulated[index] = translation;
            if (!drawn) continue;
            const std::array<float, 16> local{{
                1,0,0,translation[0], 0,1,0,translation[1],
                0,0,1,translation[2], 0,0,0,1}};

            RetainedPreparedNode node{};
            node.figure = &source_node.figure;
            node.object_local = local;
            node.object_to_world = retained_multiply_matrix(placement, local);
            node.bounds = selected_bounds(source_node.figure, object.shape);
            if (!std::all_of(node.object_to_world.begin(),
                             node.object_to_world.end(), finite) ||
                !std::all_of(node.bounds.center.begin(),
                             node.bounds.center.end(), finite) ||
                !std::all_of(node.bounds.minimum.begin(),
                             node.bounds.minimum.end(), finite) ||
                !std::all_of(node.bounds.maximum.begin(),
                             node.bounds.maximum.end(), finite) ||
                !finite_value(node.bounds.radius) || node.bounds.radius < 0.0f)
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::node_bounds,
                    source_index, index, signed_id);
            for (std::size_t axis = 0; axis < 3; ++axis)
                if (node.bounds.minimum[axis] > node.bounds.maximum[axis])
                    return prepare_failed(
                        diagnostic, RetainedStaticPrepareResult::node_bounds,
                        source_index, index, signed_id);
            if (!selected_groups(figure.positions, figure.position_groups, figure.shape_count,
                                 object.shape, node.positions))
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::position_selection,
                    source_index, index, signed_id);
            node.normals = figure.normals;

            const std::string* texture_name = figure_texture_for_type(
                object, source_node.figure.texture_number);
            if (!texture_name)
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::texture_selector,
                    source_index, index, signed_id);
            const auto texture_found = candidate->snapshot->textures.find(
                *texture_name);
            if (texture_found == candidate->snapshot->textures.end() ||
                !texture_found->second || !texture_found->second->source ||
                texture_found->second->width == 0 ||
                texture_found->second->height == 0 ||
                texture_found->second->mips.empty())
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::texture_lookup,
                    source_index, index, signed_id);
            node.texture = texture_found->second.get();
            VisualPartAsset part;
            part.name = source_node.name;
            part.geometry = std::shared_ptr<const FigureCpuAsset>(model_found->second, &source_node.figure);
            part.skeleton_node = static_cast<std::uint32_t>(index);
            part.texture = texture_found->second;
            part.texture_source_name = *texture_name;
            part.texture_type = source_node.figure.texture_number;
            part.material_type = source_node.figure.material_group;
            part.material = material;
            assembly->parts.push_back(std::move(part));
            node.texture_mips.reserve(node.texture->mips.size());
            for (const MmpMipLevel& mip : node.texture->mips) {
                if (mip.width == 0 || mip.height == 0 || mip.bytes == 0 ||
                    mip.byte_offset > node.texture->source->size() ||
                    mip.bytes > node.texture->source->size() - mip.byte_offset)
                    return prepare_failed(
                        diagnostic, RetainedStaticPrepareResult::texture_lookup,
                        source_index, index, signed_id);
                node.texture_mips.push_back({
                    node.texture->source->data() + mip.byte_offset,
                    mip.width, mip.height, mip.bytes});
            }

            std::uint64_t geometry = hash_string(
                1469598103934665603ull, object.visual_reference);
            geometry = hash_bytes(geometry, &index, sizeof(index));
            geometry = hash_bytes(geometry, object.shape.data(),
                                  sizeof(object.shape));
            geometry = hash_string(geometry, source_node.figure.identity.key());
            node.geometry_identity = folded_identity(geometry);
            node.geometry_revision = {{
                static_cast<std::uint32_t>(geometry),
                static_cast<std::uint32_t>(geometry >> 32u),
                source_node.figure.position_groups,
                source_node.figure.normal_groups,
                static_cast<std::uint32_t>(source_node.figure.indices.size())}};
            std::uint64_t texture = hash_string(
                1469598103934665603ull, node.texture->identity.key());
            texture = hash_string(texture, *texture_name);
            node.texture_identity = texture ? texture : 1u;
            node.texture_format = texture_format(node.texture->format);
            candidate->nodes.push_back(std::move(node));
        }
        if (candidate->nodes.empty())
            return prepare_failed(
                diagnostic, RetainedStaticPrepareResult::figure_structure,
                source_index, kRetainedStaticPrepareNoIndex, signed_id);
        // The root node (sway_tree_assembly's rule: exactly one) placed like
        // every node, drawn or not.
        std::size_t roots = 0, root = 0;
        for (std::size_t index = 0; index < model.links.size(); ++index)
            if (model.links[index].parent_index == UINT_MAX && model.links[index].parent.empty()) {
                ++roots; root = index;
            }
        if (roots == 1) {
            const auto& t = accumulated[root];
            for (unsigned axis = 0; axis < 3; ++axis)
                candidate->root_world[axis] = placement[axis * 4] * t[0] + placement[axis * 4 + 1] * t[1] +
                    placement[axis * 4 + 2] * t[2] + placement[axis * 4 + 3];
            candidate->root_valid = std::all_of(candidate->root_world.begin(),
                candidate->root_world.end(), finite);
        }
        add_astral_clumps(*assembly);  // astral rift veils (astral_rift.h), before the assembly is shared
        candidate->assembly = std::move(assembly);
        candidate->prepared = true;
        return true;
    } catch (...) {
        prepared.reset();
        return prepare_failed(
            diagnostic, RetainedStaticPrepareResult::exception,
            source_index, current_node, signed_id);
    }
}

bool prepare_retained_static_scene(
    const RetainedStaticSceneSnapshot& source,
    std::vector<RetainedPreparedInstance>& prepared,
    RetainedStaticPrepareDiagnostic* diagnostic) noexcept {
    prepared.clear();
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->result = RetainedStaticPrepareResult::success;
        diagnostic->binding_revision = source.binding_revision;
        diagnostic->source_bindings = source.bindings.size();
    }
    if (!source.collision_free)
        return prepare_failed(diagnostic,
                              RetainedStaticPrepareResult::source_collision);
    try {
        prepared.reserve(source.bindings.size());
        for (std::size_t index = 0; index < source.bindings.size(); ++index) {
            const RetainedStaticBinding& binding = source.bindings[index];
            if (!binding.candidate || binding.lifecycle.id <= 0 ||
                binding.lifecycle.id != binding.candidate->signed_id ||
                !binding.lifecycle.generation ||
                !binding.lifecycle.positive_world_epoch)
                return prepare_failed(
                    diagnostic, RetainedStaticPrepareResult::binding_identity,
                    index, kRetainedStaticPrepareNoIndex,
                    binding.candidate ? binding.candidate->signed_id :
                        binding.lifecycle.id);
            if (!binding.candidate->prepared) {
                if (diagnostic) {
                    *diagnostic = binding.candidate->preparation;
                    diagnostic->binding_revision = source.binding_revision;
                    diagnostic->source_bindings = source.bindings.size();
                    diagnostic->binding_index = index;
                }
                return false;
            }
            prepared.push_back({binding.lifecycle, binding.candidate});
        }
        return true;
    } catch (...) {
        prepared.clear();
        return prepare_failed(diagnostic,
                              RetainedStaticPrepareResult::exception);
    }
}

namespace {
bool finite_world_bounds(const NativeWorldBounds& box) noexcept {
    if (!box.valid) return false;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (!finite_value(box.minimum[axis]) || !finite_value(box.maximum[axis]) ||
            box.minimum[axis] > box.maximum[axis]) return false;
    return true;
}

void include_world_point(NativeWorldBounds& box, const std::array<float, 3>& p) noexcept {
    if (!box.valid) { box.minimum = box.maximum = p; box.valid = true; return; }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        box.minimum[axis] = (std::min)(box.minimum[axis], p[axis]);
        box.maximum[axis] = (std::max)(box.maximum[axis], p[axis]);
    }
}

std::array<float, 3> world_corner(const NativeWorldBounds& box, unsigned corner) noexcept {
    return {(corner & 1) ? box.maximum[0] : box.minimum[0],
            (corner & 2) ? box.maximum[1] : box.minimum[1],
            (corner & 4) ? box.maximum[2] : box.minimum[2]};
}

template<class Scalar>
bool inverse_world_clip(const std::array<Scalar, 16>& matrix,
                         std::array<double, 16>& inverse) noexcept {
    double rows[4][8]{};
    for (unsigned r = 0; r < 4; ++r) {
        for (unsigned c = 0; c < 4; ++c) {
            if (!finite_value(matrix[r*4+c])) return false;
            rows[r][c] = matrix[r*4+c];
        }
        rows[r][r+4] = 1;
    }
    for (unsigned column = 0; column < 4; ++column) {
        unsigned pivot = column;
        for (unsigned r = column+1; r < 4; ++r)
            if (std::abs(rows[r][column]) > std::abs(rows[pivot][column])) pivot = r;
        if (std::abs(rows[pivot][column]) < 1e-15) return false;
        for (unsigned c = 0; c < 8; ++c) std::swap(rows[pivot][c], rows[column][c]);
        const double divisor = rows[column][column];
        for (double& value : rows[column]) value /= divisor;
        for (unsigned r = 0; r < 4; ++r) if (r != column) {
            const double factor = rows[r][column];
            for (unsigned c = 0; c < 8; ++c) rows[r][c] -= factor*rows[column][c];
        }
    }
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c)
        inverse[r*4+c] = rows[r][c+4];
    return true;
}

NativeWorldBounds light_bounds(const NativeWorldBounds& bounds,
    const std::array<std::array<float, 3>, 3>& axes) noexcept {
    NativeWorldBounds result{};
    if (!finite_world_bounds(bounds)) return result;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const auto p = world_corner(bounds, corner);
        std::array<float, 3> projected{};
        for (unsigned axis = 0; axis < 3; ++axis)
            projected[axis] = axes[axis][0]*p[0] + axes[axis][1]*p[1] + axes[axis][2]*p[2];
        include_world_point(result, projected);
    }
    return result;
}

using ShadowPoint = std::array<double, 3>;
using ShadowPlane = std::array<double, 4>;
struct ShadowFrustum {
    std::array<ShadowPlane, 6> planes{};
    std::array<ShadowPoint, 8> corners{};
};

bool shadow_unproject(const std::array<double, 16>& inverse,
    double x, double y, double z, ShadowPoint& point) noexcept {
    const double clip[]{x, y, z, 1};
    double world[4]{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            world[row] += inverse[row*4+column]*clip[column];
    if (!(world[3] > 0) || !finite_value(world[3])) return false;
    for (unsigned axis = 0; axis < 3; ++axis) {
        point[axis] = world[axis]/world[3];
        if (!finite_value(point[axis])) return false;
    }
    return true;
}

template<class Scalar>
bool shadow_frustum(const std::array<Scalar, 16>& matrix,
    ShadowFrustum& frustum) noexcept {
    std::array<double, 16> inverse{};
    if (!inverse_world_clip(matrix, inverse)) return false;
    for (unsigned column = 0; column < 4; ++column) {
        frustum.planes[0][column] = double(matrix[12+column])+matrix[column];
        frustum.planes[1][column] = double(matrix[12+column])-matrix[column];
        frustum.planes[2][column] = double(matrix[12+column])+matrix[4+column];
        frustum.planes[3][column] = double(matrix[12+column])-matrix[4+column];
        frustum.planes[4][column] = matrix[8+column];
        frustum.planes[5][column] = double(matrix[12+column])-matrix[8+column];
    }
    for (unsigned corner = 0; corner < 8; ++corner)
        if (!shadow_unproject(inverse, corner&1 ? 1 : -1,
            corner&2 ? 1 : -1, corner&4 ? 1 : 0, frustum.corners[corner])) return false;
    return true;
}

double shadow_plane_distance(const ShadowPlane& plane, const ShadowPoint& p) noexcept {
    return plane[0]*p[0]+plane[1]*p[1]+plane[2]*p[2]+plane[3];
}

template<class Include>
void shadow_clip_edge(const ShadowPoint& a, const ShadowPoint& b,
    const std::array<ShadowPlane, 6>& planes, Include& include) noexcept {
    double first = 0, last = 1;
    for (const auto& plane : planes) {
        const double da = shadow_plane_distance(plane, a);
        const double db = shadow_plane_distance(plane, b);
        if (da < 0 && db < 0) return;
        if (da < 0) first = (std::max)(first, da/(da-db));
        if (db < 0) last = (std::min)(last, da/(da-db));
        if (first > last) return;
    }
    for (double fraction : {first, last}) {
        ShadowPoint point{};
        for (unsigned axis = 0; axis < 3; ++axis)
            point[axis] = a[axis]+fraction*(b[axis]-a[axis]);
        include(point);
    }
}

// Vertices of the intersection are corners of either solid, or an edge of
// either solid meeting a face of the other. Clip both sets of twelve edges:
// testing only receiver corners loses camera-inside and crossing-face cases.
// Fully contained/rejected boxes avoid that bounded clipping work altogether.
#ifdef EID3D11_TESTING
std::uint64_t g_shadow_receiver_clip_count=0;
#endif

template<class Include>
void shadow_receiver_points(const NativeWorldBounds& bounds,
    const ShadowFrustum& frustum, Include include) noexcept {
#ifdef EID3D11_TESTING
    ++g_shadow_receiver_clip_count;
#endif
    if (!finite_world_bounds(bounds)) return;
    std::array<ShadowPoint, 8> corners{};
    for (unsigned corner = 0; corner < 8; ++corner) {
        const auto p = world_corner(bounds, corner);
        corners[corner] = {p[0], p[1], p[2]};
    }
    bool contained = true;
    for (const auto& plane : frustum.planes) {
        unsigned inside = 0;
        for (const auto& p : corners) inside += shadow_plane_distance(plane, p) >= 0;
        if (!inside) return;
        contained &= inside == corners.size();
    }
    if (contained) {
        for (const auto& p : corners) include(p);
        return;
    }
    std::array<ShadowPlane, 6> box_planes{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        box_planes[axis*2][axis] = 1;
        box_planes[axis*2][3] = -double(bounds.minimum[axis]);
        box_planes[axis*2+1][axis] = -1;
        box_planes[axis*2+1][3] = bounds.maximum[axis];
    }
    for (unsigned corner = 0; corner < 8; ++corner)
        for (unsigned axis = 0; axis < 3; ++axis) {
            const unsigned adjacent = corner^(1u<<axis);
            if (adjacent < corner) continue;
            shadow_clip_edge(corners[corner], corners[adjacent], frustum.planes, include);
            shadow_clip_edge(frustum.corners[corner], frustum.corners[adjacent], box_planes, include);
        }
}

template<class Scalar>
double shadow_clip_depth(const std::array<Scalar, 16>& matrix,
    const ShadowPoint& point) noexcept {
    double z = matrix[11], w = matrix[15];
    for (unsigned axis = 0; axis < 3; ++axis) {
        z += double(matrix[8+axis])*point[axis];
        w += double(matrix[12+axis])*point[axis];
    }
    return z/w;
}

bool finish_shadow_receiver_xy(RetainedDirectionalSlice& slice,
    const std::array<double, 2>& center, double half_extent,
    std::uint32_t map_side) noexcept {
    if (!slice.receivers_in_light.valid) return true;
    auto& bounds = slice.receivers_in_light;
    double magnitude = 1;
    for (unsigned axis = 0; axis < 2; ++axis) {
        magnitude = (std::max)({magnitude, std::abs(center[axis]),
            std::abs(double(bounds.minimum[axis])), std::abs(double(bounds.maximum[axis]))});
    }
    // Rendering policy: 16..32 radius steps per octave (at most 6.25% outward
    // rounding). Small coverage changes keep a fixed texel scale without a
    // temporal cache. Crossing a size step deliberately refits the projection.
    // Reserve the 2.5-texel filter support, half a center-snap texel, and
    // float roundoff before rounding the radius to a stable size step.
    double radius = (std::max)(half_extent/(1-10.0/map_side),
        magnitude*std::numeric_limits<float>::epsilon()*map_side);
    int exponent = 0;
    std::frexp(radius, &exponent);
    const double quantum = (std::max)(0.5,std::ldexp(1.0, exponent-5));
    radius = std::ceil(radius/quantum)*quantum;
    slice.light_radius = static_cast<float>(radius);
    if (!finite_value(slice.light_radius) || slice.light_radius <= 0) return false;
    const double texel = 2*radius/map_side;
    for (unsigned axis = 0; axis < 2; ++axis) {
        slice.light_center[axis] = static_cast<float>(std::floor(center[axis]/texel+.5)*texel);
        bounds.minimum[axis] = next_float(static_cast<float>(bounds.minimum[axis]-3*texel), -INFINITY);
        bounds.maximum[axis] = next_float(static_cast<float>(bounds.maximum[axis]+3*texel), INFINITY);
        if (double(slice.light_center[axis])-radius > bounds.minimum[axis] ||
            double(slice.light_center[axis])+radius < bounds.maximum[axis]) return false;
    }
    return true;
}

bool fit_shadow_receiver_xy(RetainedDirectionalSlice& slice,
    std::uint32_t map_side,double minimum_half_extent=0) noexcept {
    if (!slice.receivers_in_light.valid) return true;
    std::array<double, 2> center{};
    double half_extent = 0;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const auto& bounds = slice.receivers_in_light;
        center[axis] = (double(bounds.minimum[axis])+bounds.maximum[axis])*.5;
        half_extent = (std::max)(half_extent, (double(bounds.maximum[axis])-bounds.minimum[axis])*.5);
    }
    return finish_shadow_receiver_xy(slice, center,
        (std::max)(half_extent,minimum_half_extent),map_side);
}

bool fit_shadow_camera_slice_xy(RetainedDirectionalSlice& slice,
    const ShadowFrustum& section,
    const std::array<std::array<float, 3>, 3>& light_axes,
    std::uint32_t map_side) noexcept {
    if (!slice.receivers_in_light.valid) return true;
    ShadowPoint near_center{}, far_center{}, delta{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (unsigned corner = 0; corner < 4; ++corner) {
            near_center[axis] += section.corners[corner][axis]*.25;
            far_center[axis] += section.corners[corner+4][axis]*.25;
        }
        delta[axis] = far_center[axis]-near_center[axis];
    }
    const auto squared_distance = [](const ShadowPoint& a, const ShadowPoint& b) {
        double sum = 0;
        for (unsigned axis = 0; axis < 3; ++axis) sum += (a[axis]-b[axis])*(a[axis]-b[axis]);
        return sum;
    };
    const double length_squared = squared_distance(near_center, far_center);
    if (!(length_squared > 0)) return false;
    double near_radius_squared = 0, far_radius_squared = 0;
    for (unsigned corner = 0; corner < 4; ++corner) {
        near_radius_squared = (std::max)(near_radius_squared,
            squared_distance(section.corners[corner], near_center));
        far_radius_squared = (std::max)(far_radius_squared,
            squared_distance(section.corners[corner+4], far_center));
    }
    const double fraction = std::clamp(
        (length_squared+far_radius_squared-near_radius_squared)/(2*length_squared), 0.0, 1.0);
    ShadowPoint center{};
    for (unsigned axis = 0; axis < 3; ++axis) center[axis] = near_center[axis]+fraction*delta[axis];
    // A sphere covering all eight camera-slice corners has an orientation-
    // independent extent. Actual receiver bounds still own contribution and Z.
    double radius_squared = 0;
    for (const auto& corner : section.corners)
        radius_squared = (std::max)(radius_squared, squared_distance(corner, center));
    std::array<double, 2> light_center{};
    for (unsigned axis = 0; axis < 2; ++axis)
        for (unsigned component = 0; component < 3; ++component)
            light_center[axis] += double(light_axes[axis][component])*center[component];
    return finish_shadow_receiver_xy(slice, light_center,
        std::sqrt(radius_squared),map_side);
}

bool base_terrain_quality_anchor(const std::array<double,16>& camera_matrix,
    const TerrainCpuAsset& terrain, const std::array<float,16>& terrain_to_world,
    std::uint64_t terrain_generation,
    ShadowPoint& world_hit, double& ground_near, double& ground_far) noexcept {
    std::array<float,16> normalized_camera{};
    for(unsigned index=0;index<16;++index)
        normalized_camera[index]=static_cast<float>(camera_matrix[index]);
    const auto local_to_clip=retained_multiply_matrix(normalized_camera,terrain_to_world);
    std::array<double,16> inverse{};
    if (!inverse_world_clip(local_to_clip,inverse)) return false;
    ShadowPoint bottom_near{},bottom_far{},top_near{},top_far{};
    if (!shadow_unproject(inverse,0,-1,0,bottom_near) ||
        !shadow_unproject(inverse,0,-1,1,bottom_far) ||
        !shadow_unproject(inverse,0,1,0,top_near) ||
        !shadow_unproject(inverse,0,1,1,top_far)) return false;
    const double bottom_dz=bottom_far[2]-bottom_near[2];
    const double top_dz=top_far[2]-top_near[2];
    double lower=-1,upper=1;
    if (bottom_dz>=0 && top_dz>=0) return false;
    if (bottom_dz>=0 || top_dz>=0) {
        const double denominator=top_dz-bottom_dz;
        if (std::abs(denominator)<1e-12) return false;
        const double horizon=std::clamp(-1-2*bottom_dz/denominator,-1.0,1.0);
        if (bottom_dz<0) upper=horizon;
        else lower=horizon;
    }
    const double screen_y=(lower+upper)*.5;
    ShadowPoint near_point{},far_point{};
    if (!shadow_unproject(inverse,0,screen_y,0,near_point) ||
        !shadow_unproject(inverse,0,screen_y,1,far_point)) return false;
    ShadowPoint direction{};
    for (unsigned axis=0;axis<3;++axis) direction[axis]=far_point[axis]-near_point[axis];
    ShadowPoint local_hit{};
    // Looking across the map edge at rendered neighbour ground, the view axis
    // leaves the active terrain. Dropping the anchor there switched the whole
    // cascade policy (spatial off: C2 50 vs 120 units, C3 240 vs 480, C4 off
    // vs on in the ba005a6d dumps), so neighbour pixels jumped between maps
    // of very different resolution and softness while panning. The axis then
    // meets the horizontal plane at the last terrain hit's height: the same
    // reference ground the bands use below, held across the edge.
    // Held per terrain source generation: another world never uses this
    // map's height.
    static double held_height=0;static bool held=false;static std::uint64_t held_generation=0;
    if (held_generation!=terrain_generation) {held=false;held_generation=terrain_generation;}
    if (intersect_base_terrain_ray(terrain,near_point,direction,local_hit)) {
        held_height=local_hit[2];held=true;
    } else {
        if (!held || !(direction[2]<0)) return false;
        const double fraction=(held_height-near_point[2])/direction[2];
        if (!(fraction>0) || !finite_value(fraction)) return false;
        for (unsigned axis=0;axis<3;++axis) local_hit[axis]=near_point[axis]+fraction*direction[axis];
    }
    double w=terrain_to_world[15];
    for (unsigned axis=0;axis<3;++axis) w+=terrain_to_world[12+axis]*local_hit[axis];
    if (!(w>0) || !finite_value(w)) return false;
    for (unsigned axis=0;axis<3;++axis) {
        double value=terrain_to_world[axis*4+3];
        for (unsigned component=0;component<3;++component)
            value+=terrain_to_world[axis*4+component]*local_hit[component];
        world_hit[axis]=value/w;
        if (!finite_value(world_hit[axis])) return false;
    }

    // The camera's lower view rays, rather than its clip near plane, define
    // where the pitched view first spends shadow resolution on terrain. Their
    // reference ground is the horizontal plane through the view-axis terrain
    // hit. The game camera orbits its terrain anchor on that axis at a fixed
    // distance, so this plane keeps the bands fixed while panning. Terrain
    // below the physical eye is unrelated to the view: a telephoto camera
    // pulled back along its axis sees its relief change (and leave the map)
    // independently of the visible ground.
    const auto local_w=[&](const ShadowPoint& point) noexcept {
        double distance=local_to_clip[15];
        for(unsigned axis=0;axis<3;++axis)
            distance+=local_to_clip[12+axis]*point[axis];
        return distance;
    };
    const double height=local_hit[2];
    ground_near=INFINITY;ground_far=0;
    for(double sample_y:{-1.0,0.0,0.5}) {
        ShadowPoint ray_near{},ray_far{};
        if(!shadow_unproject(inverse,0,sample_y,0,ray_near) ||
            !shadow_unproject(inverse,0,sample_y,1,ray_far)) return false;
        const double dz=ray_far[2]-ray_near[2];
        if (!(dz<0)) continue;
        const double fraction=(double(height)-ray_near[2])/dz;
        if (!(fraction>0)) continue;
        ShadowPoint ground{};
        for(unsigned axis=0;axis<3;++axis)
            ground[axis]=ray_near[axis]+fraction*(ray_far[axis]-ray_near[axis]);
        const double distance=local_w(ground);
        if (!(distance>0) || !finite_value(distance)) continue;
        ground_near=(std::min)(ground_near,distance);
        ground_far=(std::max)(ground_far,distance);
    }
    if (!finite_value(ground_near) || !(ground_far>=ground_near)) return false;
    return true;
}
}

const std::array<float,3>& retained_visual_shape(const RetainedVisualSample& sample) noexcept {
    return sample.values.unit && !sample.completed_pose && sample.assembly ? sample.assembly->shape : sample.object_pose.shape;
}

namespace {
void prepare_physique(const RetainedVisualSample& sample,const gltf::Asset& body){
 auto& p=*sample.prepared;
 if(p.physique_source==body.gpu_animation && p.motion_physique_input==sample.physique)return;
 auto pose=body.physique.resolve(sample.physique,unsigned(body.animation->nodes.size()),body.cache_key);
 std::shared_ptr<gltf::PhysiqueSurface> surface;
 if(pose){
  surface=std::make_shared<gltf::PhysiqueSurface>();
  surface->build(body.physique,*pose,body.rest_vertices,body.geometry.joints);
 }
 p.motion_physique=std::move(pose);p.physique_surface=std::move(surface);
 p.motion_physique_input=sample.physique;p.physique_source=body.gpu_animation;
 p.sole_valid=false;
}
const gltf::EquipmentSelection& prepare_gltf_equipment(const RetainedVisualSample& sample,
    const gltf::Asset& body) {
 auto& prepared=*sample.prepared;
 prepare_physique(sample,body);
 const bool same=prepared.gltf_equipment_assembly==sample.assembly && prepared.gltf_equipment;
 if(same && prepared.equipment_physique==prepared.motion_physique)return *prepared.gltf_equipment;
 auto selected=std::make_shared<gltf::EquipmentSelection>();
 selected->items=same?prepared.gltf_equipment->items:gltf::select_equipment(sample.assembly->equipment,body.resource_key);
 auto limits=body.node_envelopes;
 if(prepared.motion_physique){
  for(unsigned i=0;i<limits.size();++i){
   auto& v=limits[i];v.translation=v.local_translation;v.scale=v.local_scale;
   const auto& change=prepared.motion_physique->locals[i];
   if(change.mode){
    double ns=0,inf=0;
    for(unsigned r=0;r<3;++r){
     double a=0,b=0;for(unsigned c=0;c<3;++c){a+=std::abs(change.rows[r*4+c]);b+=std::abs(change.rows[c*4+r]);}
     inf=(std::max)(inf,a);ns=(std::max)(ns,b);
    }
    v.scale*=std::sqrt(ns*inf);
    v.translation+=std::hypot(change.rows[3],change.rows[7],change.rows[11]);
   }
   const int parent=body.animation->nodes[i].parent;
   if(parent>=0){v.translation=limits[parent].translation+limits[parent].scale*v.translation;v.scale*=limits[parent].scale;}
  }
 }
 const auto shapedRadius=[&](const std::vector<skn::Vertex>& vertices,const auto& inverse,const auto& joint,
                            const std::vector<gltf::GarmentAnchor>* anchors){
  double radius=0;
  for(size_t i=0;i<vertices.size();++i){
   auto v=vertices[i];
   if(anchors)prepared.physique_surface->apply_garment((*anchors)[i],v);
   double bound=0;
   for(unsigned k=0;k<4;++k)if(v.weights[k]){
    const auto p=skn::point(inverse(v.joints[k]),v.position);
    const auto& lim=limits.at(joint(v.joints[k]));
    bound+=v.weights[k]/65535.*(lim.translation+lim.scale*std::hypot(p[0],p[1],p[2]));
   }
   radius=(std::max)(radius,bound);
  }
  return float(radius*(1+1e-5));
 };
 selected->radius=body.animation_radius;
 if(prepared.physique_surface){
  const auto& surface=*prepared.physique_surface;
  selected->radius=shapedRadius(surface.body,[&](unsigned j)->const auto&{return surface.inverse_skin[j];},
      [](unsigned j){return j;},nullptr);
 }
 const double bodyRadius=selected->radius;
 for(auto& item:selected->items) {
  if(item.asset->wearable) {
   if(!same)item.body_nodes=gltf::wearable_nodes(*item.asset,body);
   float radius=0;
   if(prepared.physique_surface){
    if(!item.physique_anchors)item.physique_anchors=std::make_shared<const std::vector<gltf::GarmentAnchor>>(
        body.physique_tree.map(body.rest_vertices,item.asset->rest_vertices));
    radius=shapedRadius(item.asset->rest_vertices,[&](unsigned j)->const auto&{return item.asset->geometry.joints[j].inverse;},
        [&](unsigned j){return item.body_nodes[j];},item.physique_anchors.get());
   }else radius=gltf::wearable_radius(*item.asset,body,item.body_nodes);
   selected->radius=(std::max)(selected->radius,radius);
   continue;
  }
  auto found=std::find_if(body.geometry.joints.begin(),body.geometry.joints.end(),
      [&](const skn::Joint& j){return j.name==item.socket;});
  if(found==body.geometry.joints.end())throw std::runtime_error("glTF equipment socket missing in body");
  if(!item.asset->mechanism.empty() || item.asset->projectile_aim) {
   item.mechanism_offset=selected->mechanism_nodes;
   selected->mechanism_nodes+=item.asset->projectile_aim ? 1u : unsigned(item.asset->mechanism.size());
  }
  const auto& limit=limits.at(found-body.geometry.joints.begin());
  const double radius=limit.translation+limit.scale*(item.asset->radius+
      std::hypot(item.offset[0],item.offset[1],item.offset[2]));
  if(!std::isfinite(radius)||radius>FLT_MAX/4)throw std::runtime_error("glTF equipment bounds overflow");
  selected->radius=(std::max)(selected->radius,static_cast<float>(radius)*(1+1e-5f));
  if(item.asset->bow_constraint || item.asset->projectile_aim) {
   // Constrained endpoints can travel with either hand, beyond the original
   // item's local animation envelope. Keep a conservative common body bound.
   const double expanded=bodyRadius+radius+2*limit.scale*item.asset->radius;
   if(!std::isfinite(expanded)||expanded>FLT_MAX/4)throw std::runtime_error("weapon constraint bounds overflow");
   selected->radius=(std::max)(selected->radius,float(expanded)*(1+1e-5f));
  }
 }
 const gltf::WeaponTwoHandSupport* support=nullptr;
 for(const auto& item:selected->items)if(item.asset->two_hand_support && item.slot=="hand_r") {
  if(support){support=nullptr;break;}
  support=item.asset->two_hand_support.get();
 }
 if(support)selected->support_arm=same && prepared.gltf_equipment->support_arm ?
     prepared.gltf_equipment->support_arm:gltf::weapon_arm_plan(body,*support);
 // A translated root keeps the original sole midpoint. Its shift is bounded
 // by both feet's old and shaped envelopes, including their local sole points.
 if(prepared.motion_physique){
  double shift=0;
  for(unsigned k=0;k<2;++k){
   const auto j=body.sole_nodes[k];const auto& p=body.sole_points[k];
   const auto& a=limits[j];const auto& b=body.node_envelopes[j];
   shift+=.5*(a.translation+b.translation+(a.scale+b.scale)*std::hypot(p[0],p[1],p[2]));
  }
  selected->radius+=float(shift);
 }
 prepared.equipment_physique=prepared.motion_physique;
 prepared.gltf_equipment=std::move(selected);prepared.gltf_equipment_assembly=sample.assembly;
 return *prepared.gltf_equipment;
}
bool skn_pose_bounds(const RetainedVisualSample& sample,NativeWorldBounds& bounds,bool& present){
 present=false;
 if(!sample.values.unit||!sample.assembly||!sample.assembly->skeleton)return true;
 auto& prepared=*sample.prepared;
 if(prepared.merged_assembly==sample.assembly&&!prepared.skn_selected){
  prepared.skn_bounds_asset.reset();prepared.skn_bounds_valid=false;return true;
 }
 std::shared_ptr<const skn::RuntimeAsset> asset;
 if(prepared.skn_bounds_asset&&prepared.skn_bounds_key.assembly==sample.assembly){asset=prepared.skn_bounds_asset;present=true;}
 else asset=skn::select_visual(sample.assembly->skeleton->visual_reference,sample.assembly->main_hero_visual,present);
 if(!present){prepared.skn_bounds_asset.reset();prepared.skn_bounds_valid=false;return true;}
 if(prepared.skn_bounds_valid&&prepared.skn_bounds_physique==sample.physique&&asset==prepared.skn_bounds_asset&&owned_visual_pose_key_matches(prepared.skn_bounds_key,sample)){
  bounds=prepared.skn_bounds;return true;
 }
 if(asset->gltf_asset) {
  const float radius=prepare_gltf_equipment(sample,*asset->gltf_asset).radius*(std::max)({std::abs(sample.assembly->scale[0]),std::abs(sample.assembly->scale[1]),std::abs(sample.assembly->scale[2])})+1e-4f;
  for(unsigned axis=0;axis<3;++axis) {
   const float position=sample.completed_pose?sample.object_pose.position[axis]:sample.pose.root[axis];
   bounds.minimum[axis]=position-radius;bounds.maximum[axis]=position+radius;
  }
  bounds.valid=true;
  if(!finite_world_bounds(bounds))return false;
  prepared.skn_bounds_asset=asset;prepared.skn_bounds_key=owned_visual_pose_key(sample);
  prepared.skn_bounds_physique=sample.physique;prepared.skn_bounds=bounds;prepared.skn_bounds_valid=true;return true;
 }
 std::vector<VisualNodeSample> pose;
 if(!sample_visual_skeleton(*sample.assembly->skeleton,sample.selection,retained_visual_shape(sample),
     sample.assembly->center_shift,sample.pose.phase,pose,false))return false;
 bounds.minimum.fill(FLT_MAX);bounds.maximum.fill(-FLT_MAX);
 const auto node_matrix=[&](unsigned node,std::array<float,16>& matrix){
  if(sample.completed_pose){
   VisualPoseState local;local.tilted_facing=local.root_rotation_valid=true;local.root_rotation={1,0,0,0};
   std::array<float,16> placement{};
   if(!visual_node_world_matrix(local,pose[node],sample.assembly->scale,matrix)||
      !retained_object_matrix(sample.object_pose.position,sample.object_pose.quaternion,placement))return false;
   matrix=retained_multiply_matrix(placement,matrix);
  }else if(!visual_node_world_matrix(sample.pose,pose[node],sample.assembly->scale,matrix))return false;
  return true;
 };
 std::array<float,16> node_zero{};
 if(pose.empty()||!node_matrix(0,node_zero))return false;
 for(unsigned axis=0;axis<3;++axis)bounds.minimum[axis]=bounds.maximum[axis]=node_zero[axis*4+3];
 // Baked affine poses and post-skin targets are not bounded by legacy BON
 // spheres. The cooked model-space envelope covers every declared clip.
 if(asset->motion){
  const double scale=(std::max)({std::abs(sample.assembly->scale[0]),std::abs(sample.assembly->scale[1]),std::abs(sample.assembly->scale[2])});
  const float radius=static_cast<float>(asset->motion->radius*scale);
  for(unsigned axis=0;axis<3;++axis){bounds.minimum[axis]-=radius;bounds.maximum[axis]+=radius;}
 }
 std::unordered_map<std::string,unsigned> nodes;
 nodes.reserve(sample.assembly->skeleton->nodes.size());
 for(unsigned n=0;n<sample.assembly->skeleton->nodes.size();++n)nodes.emplace(sample.assembly->skeleton->nodes[n].name,n);
 if(!asset->motion)for(unsigned j=0;j<asset->joint_radius.size();++j){
  if(asset->joint_radius[j]<0)continue;
  const auto found=nodes.find(asset->asset.joints[j].name);
  if(found==nodes.end())return false;
  const unsigned node=found->second;
  if(node>=pose.size())return false;
  std::array<float,16> matrix{};
  if(!node_matrix(node,matrix))return false;
  for(unsigned axis=0;axis<3;++axis){
   double squared=0;for(unsigned k=0;k<3;++k)squared+=double(matrix[axis*4+k])*matrix[axis*4+k];
   const double radius=(std::max)(0.f,asset->joint_radius[j])*std::sqrt(squared)+1e-5;
   bounds.minimum[axis]=(std::min)(bounds.minimum[axis],next_float(static_cast<float>(matrix[axis*4+3]-radius),-INFINITY));
   bounds.maximum[axis]=(std::max)(bounds.maximum[axis],next_float(static_cast<float>(matrix[axis*4+3]+radius),INFINITY));
  }
 }
 bounds.valid=true;if(!finite_world_bounds(bounds))return false;
 prepared.skn_bounds_asset=asset;prepared.skn_bounds_key=owned_visual_pose_key(sample);
 prepared.skn_bounds=bounds;prepared.skn_bounds_valid=true;return true;
}
}
bool retained_visual_world_bounds(const RetainedVisualSample& sample,
                                   NativeWorldBounds& bounds) noexcept {
    bounds = {};
    if (sample.status != RetainedVisualStatus::ready) return false;
    bool skn_present=false;
    try{if(!skn_pose_bounds(sample,bounds,skn_present))return false;if(skn_present)return true;}
    catch(const std::exception& error){skn::report_rejection(sample.assembly->skeleton->visual_reference,error.what());return false;}
    float envelope_radius = sample.envelope_radius;
    if(sample.assembly && astral_model(*sample.assembly))envelope_radius+=7.f;
    if (sample.values.unit && !sample.completed_pose &&
        !retained_visual_selection_envelope_radius(sample,envelope_radius)) return false;
    // Astral rift clumps drift up to kAstralClumpMargin beyond the rest shape.
    if (sample.assembly && astral_model(*sample.assembly)) envelope_radius += kAstralClumpMargin;
    if (!finite_value(envelope_radius) || envelope_radius < 0) return false;
    std::array<float,3> extent{envelope_radius,envelope_radius,envelope_radius};
    auto root = sample.pose.root;
    if (!sample.values.unit || sample.completed_pose) {
        std::array<float,16> placement{};
        root = sample.object_pose.position;
        if (!retained_object_matrix(root,sample.object_pose.quaternion,placement)) return false;
        for (unsigned row = 0; row < 3; ++row) {
            double squared = 0;
            for (unsigned column = 0; column < 3; ++column) {
                const double value = placement[row*4+column];
                squared += value*value;
            }
            extent[row] = static_cast<float>(envelope_radius*std::sqrt(squared));
        }
    }
    if (sample.values.unit && !sample.completed_pose && sample.pose.tilted_facing) {
        if (!sample.pose.root_rotation_valid) return false;
        double magnitude_squared = 0;
        for (float value : sample.pose.root_rotation) {
            if (!finite_value(value)) return false;
            magnitude_squared += double(value)*value;
        }
        if (magnitude_squared == 0) return false;
        const double radius = double(envelope_radius)*magnitude_squared;
        float rounded = static_cast<float>(radius);
        if (double(rounded) < radius) rounded = next_float(rounded,INFINITY);
        extent.fill(rounded);
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        bounds.minimum[axis] = next_float(root[axis]-extent[axis],-INFINITY);
        bounds.maximum[axis] = next_float(root[axis]+extent[axis],INFINITY);
    }
    bounds.valid = true;
    return finite_world_bounds(bounds);
}

bool retained_world_bounds_inside(const NativeWorldBounds& bounds,
    const std::array<float, 16>& m) noexcept {
    if (!finite_world_bounds(bounds)) return false;
    for (unsigned corner = 0; corner < 8u; ++corner) {
        const float x = (corner & 1u) ? bounds.maximum[0] : bounds.minimum[0];
        const float y = (corner & 2u) ? bounds.maximum[1] : bounds.minimum[1];
        const float z = (corner & 4u) ? bounds.maximum[2] : bounds.minimum[2];
        const float cx = m[0]*x + m[1]*y + m[2]*z + m[3];
        const float cy = m[4]*x + m[5]*y + m[6]*z + m[7];
        const float cz = m[8]*x + m[9]*y + m[10]*z + m[11];
        const float cw = m[12]*x + m[13]*y + m[14]*z + m[15];
        if (!(cx >= -cw && cx <= cw && cy >= -cw && cy <= cw && cz >= 0.0f && cz <= cw)) return false;
    }
    return true;
}

bool retained_world_bounds_visible(const NativeWorldBounds& bounds,
    const std::array<float, 16>& world_to_clip) noexcept {
    if (!finite_world_bounds(bounds)) return false;
    FigureBounds box{}; box.minimum = bounds.minimum; box.maximum = bounds.maximum;
    return retained_bounds_visible(box, world_to_clip);
}

namespace {
constexpr unsigned far_receiver_grid_side=128;

bool far_receiver_cell_range(const NativeWorldBounds& bounds,
    const RetainedDirectionalSlice& slice,double padding,
    std::array<unsigned,2>& first,std::array<unsigned,2>& last) noexcept {
    if(!bounds.valid || !(slice.light_radius>0))return false;
    for(unsigned axis=0;axis<2;++axis){
        const double minimum=double(slice.light_center[axis])-slice.light_radius;
        const double maximum=double(slice.light_center[axis])+slice.light_radius;
        if(double(bounds.maximum[axis])+padding<minimum ||
           double(bounds.minimum[axis])-padding>maximum)return false;
        const double scale=far_receiver_grid_side/(maximum-minimum);
        first[axis]=static_cast<unsigned>(std::clamp(
            std::floor((double(bounds.minimum[axis])-padding-minimum)*scale),
            0.0,double(far_receiver_grid_side-1)));
        last[axis]=static_cast<unsigned>(std::clamp(
            std::floor((double(bounds.maximum[axis])+padding-minimum)*scale),
            0.0,double(far_receiver_grid_side-1)));
    }
    return true;
}

bool far_receiver_cells_intersect(const NativeWorldBounds& caster,
    const RetainedDirectionalSlice& slice) noexcept {
    if(!slice.far_receivers_in_light.valid ||
       caster.maximum[0]<slice.far_receivers_in_light.minimum[0] ||
       caster.minimum[0]>slice.far_receivers_in_light.maximum[0] ||
       caster.maximum[1]<slice.far_receivers_in_light.minimum[1] ||
       caster.minimum[1]>slice.far_receivers_in_light.maximum[1])return false;
    std::array<unsigned,2> first{},last{};
    if(!far_receiver_cell_range(caster,slice,0,first,last))return false;
    for(unsigned y=first[1];y<=last[1];++y)
        for(unsigned x=first[0];x<=last[0];++x){
            const unsigned bit=y*far_receiver_grid_side+x;
            if(slice.far_receiver_cells[bit/64]&(std::uint64_t{1}<<(bit%64)))return true;
        }
    return false;
}
}

std::uint64_t receiver_cell_key(float cell,const NativeWorldBounds& b) noexcept {
    const auto index=[&](unsigned axis){return static_cast<std::uint32_t>(static_cast<std::int32_t>(
        std::floor((double(b.minimum[axis])+b.maximum[axis])*.5/cell)));};
    return (std::uint64_t(index(0))<<32)|index(1);
}

bool append_cell_merged_receivers(float cell,const std::vector<NativeWorldBounds>& statics,
    std::vector<NativeWorldBounds>& receivers) {
    return append_cell_merged_receivers(cell,statics,{},receivers);
}

bool append_cell_merged_receivers(float cell,const std::vector<NativeWorldBounds>& statics,
    const std::vector<std::pair<std::uint64_t,NativeWorldBounds>>& cells,
    std::vector<NativeWorldBounds>& receivers) {
    if(!(cell>0)){receivers.insert(receivers.end(),statics.begin(),statics.end());return true;}
    std::vector<std::pair<std::uint64_t,NativeWorldBounds>> keyed;keyed.reserve(statics.size());
    for(const auto& b:statics) {
        if(!b.valid)continue;
        keyed.emplace_back(receiver_cell_key(cell,b),b);
    }
    std::stable_sort(keyed.begin(),keyed.end(),[](const auto& x,const auto& y){return x.first<y.first;});
    // Per key, the union of the formed cell (if any) and every keyed box,
    // in ascending key order: min and max are exact, so the union does not
    // depend on how the boxes were grouped.
    const auto include=[](NativeWorldBounds& merged,const NativeWorldBounds& b) {
        if(!merged.valid){merged=b;return;}
        for(unsigned axis=0;axis<3;++axis){
            merged.minimum[axis]=(std::min)(merged.minimum[axis],b.minimum[axis]);
            merged.maximum[axis]=(std::max)(merged.maximum[axis],b.maximum[axis]);
        }
    };
    std::size_t i=0,c=0;
    while(i<keyed.size() || c<cells.size()) {
        const auto key=c<cells.size() && (i>=keyed.size() || cells[c].first<=keyed[i].first) ?
            cells[c].first : keyed[i].first;
        NativeWorldBounds merged{};
        if(c<cells.size() && cells[c].first==key)include(merged,cells[c++].second);
        for(;i<keyed.size() && keyed[i].first==key;++i)include(merged,keyed[i].second);
        receivers.push_back(merged);
    }
    return true;
}

ReceiverCellClass classify_receiver_cell(const NativeWorldBounds& b,
    const std::array<float,16>& m) noexcept {
    if(!finite_world_bounds(b))return ReceiverCellClass::partial;
    // Each clip row of any point in the box, evaluated in float as
    // retained_bounds_visible does, is within ~4e-7 of the row's magnitude
    // bound (sum of |coefficient| * max |coordinate|). A margin of 1e-5 of
    // the compared rows' magnitudes leaves every member box's float test
    // the same as the exact sign at the cell corners.
    std::array<double,4> scale{};
    for(unsigned row=0;row<4;++row) {
        double sum=std::abs(double(m[row*4+3]));
        for(unsigned axis=0;axis<3;++axis)
            sum+=std::abs(double(m[row*4+axis]))*
                (std::max)(std::abs(double(b.minimum[axis])),std::abs(double(b.maximum[axis])));
        if(!std::isfinite(sum))return ReceiverCellClass::partial;
        scale[row]=sum;
    }
    constexpr double k=1e-5;
    const double margin[6]{k*(scale[0]+scale[3]),k*(scale[0]+scale[3]),k*(scale[1]+scale[3]),
        k*(scale[1]+scale[3]),k*scale[2],k*(scale[2]+scale[3])};
    bool inside=true;std::uint32_t outside=0x3fu;
    for(unsigned corner=0;corner<8u;++corner) {
        const double x=(corner&1u)?b.maximum[0]:b.minimum[0];
        const double y=(corner&2u)?b.maximum[1]:b.minimum[1];
        const double z=(corner&4u)?b.maximum[2]:b.minimum[2];
        std::array<double,4> clip{};
        for(unsigned row=0;row<4;++row)
            clip[row]=double(m[row*4])*x+double(m[row*4+1])*y+double(m[row*4+2])*z+double(m[row*4+3]);
        // retained_bounds_visible's planes: x>=-w, x<=w, y>=-w, y<=w, z>=0, z<=w.
        const double distance[6]{clip[0]+clip[3],clip[3]-clip[0],clip[1]+clip[3],
            clip[3]-clip[1],clip[2],clip[3]-clip[2]};
        std::uint32_t mask=0;
        for(unsigned plane=0;plane<6;++plane) {
            if(!(distance[plane]>margin[plane]))inside=false;
            if(distance[plane]< -margin[plane])mask|=1u<<plane;
        }
        outside&=mask;
    }
    if(outside)return ReceiverCellClass::outside;
    return inside?ReceiverCellClass::inside:ReceiverCellClass::partial;
}

bool build_retained_directional_coverage(
    const std::array<float, 16>& world_to_clip, const NativeWorldLighting& lighting,
    const std::vector<NativeWorldBounds>& receiving_bounds,
    RetainedDirectionalCoverage& result, const TerrainCpuAsset* base_terrain,
    const std::array<float,16>* terrain_to_world,
    std::uint32_t map_side,std::uint8_t close_cascades,std::uint64_t base_terrain_generation,
    float close_range,float camera_anchor_z) noexcept {
    result = {};
    if(map_side<1024 || map_side>4096 || (map_side&(map_side-1)) ||
       close_cascades<1 || close_cascades>3)return false;
    result.map_side=map_side;
    // Positive homogeneous scaling cannot change visibility or view distance.
    // Canonical perspective W measures distance along the unit view direction;
    // affine cameras retain the existing receiver-fitting policy below.
    double w_length_squared = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        w_length_squared += double(world_to_clip[12+axis])*world_to_clip[12+axis];
    const bool perspective = w_length_squared > 0;
    const double camera_scale = perspective ? std::sqrt(w_length_squared) : world_to_clip[15];
    if (!(camera_scale > 0) || !finite_value(camera_scale)) return false;
    std::array<double, 16> camera_matrix{};
    for (unsigned index = 0; index < 16; ++index) camera_matrix[index] = world_to_clip[index]/camera_scale;
    std::array<double, 16> inverse{};
    if (!inverse_world_clip(camera_matrix, inverse)) return false;
    double ray_length = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!finite_value(lighting.sunlight_direction[axis]) ||
            !finite_value(lighting.sunlight_diffuse[axis])) return false;
        ray_length += static_cast<double>(lighting.sunlight_direction[axis]) *
            lighting.sunlight_direction[axis];
        result.directional |= lighting.sunlight_diffuse[axis] > 0;
    }
    if (ray_length <= 1e-12) return false;
    const double divisor = std::sqrt(ray_length);
    for (unsigned axis = 0; axis < 3; ++axis)
        result.light_axes[2][axis] = static_cast<float>(-lighting.sunlight_direction[axis]/divisor);
    const auto& ray = result.light_axes[2];
    auto& u = result.light_axes[0];
    // Project world X onto the light plane. The shipped outdoor/cave sun rays
    // stay away from +/-X, so time-of-day cannot switch this frame's axes.
    // Only an exact +/-X input needs the fixed world-Y fallback.
    const double y = ray[1], z = ray[2], x = ray[0];
    const std::array<double,3> projected_x{y*y+z*z,-x*y,-x*z};
    const double projected_length = std::sqrt(projected_x[0]*projected_x[0] +
        projected_x[1]*projected_x[1] + projected_x[2]*projected_x[2]);
    if (projected_length > 0) {
        for (unsigned axis = 0; axis < 3; ++axis)
            u[axis] = static_cast<float>(projected_x[axis]/projected_length);
    } else {
        u = {0,1,0};
    }
    result.light_axes[1] = {ray[1]*u[2]-ray[2]*u[1], ray[2]*u[0]-ray[0]*u[2],
                           ray[0]*u[1]-ray[1]*u[0]};
    result.world_to_clip = world_to_clip;
    ShadowFrustum camera{};
    if (!shadow_frustum(camera_matrix, camera)) return false;
    double minimum_depth = 1, maximum_depth = 0;
    bool receivers = false;
    // Each receiver is clipped against the camera once; the spatial pass below
    // reuses these points instead of clipping the same box again.
    std::vector<ShadowPoint> camera_points;std::vector<std::uint32_t> camera_point_offsets;
    try { camera_point_offsets.reserve(receiving_bounds.size()+1); camera_points.reserve(receiving_bounds.size()*8); }
    catch (...) { return false; }
    for (const auto& receiver : receiving_bounds) {
        camera_point_offsets.push_back(static_cast<std::uint32_t>(camera_points.size()));
        shadow_receiver_points(receiver, camera, [&](const ShadowPoint& point) {
            const double depth = shadow_clip_depth(camera_matrix, point);
            minimum_depth = (std::min)(minimum_depth, depth);
            maximum_depth = (std::max)(maximum_depth, depth);
            receivers = true;
            try { camera_points.push_back(point); } catch (...) {}
        });
    }
    camera_point_offsets.push_back(static_cast<std::uint32_t>(camera_points.size()));
    if (!receivers) { result.valid = true; return true; }
    minimum_depth = (std::max)(0.0, minimum_depth);
    maximum_depth = (std::min)(1.0, maximum_depth);
    if (!finite_value(minimum_depth) || !finite_value(maximum_depth) || minimum_depth > maximum_depth) return false;

    ShadowPoint near_point{}, far_point{};
    if (!shadow_unproject(inverse, 0, 0, perspective ? 0 : minimum_depth, near_point) ||
        !shadow_unproject(inverse, 0, 0, perspective ? 1 : maximum_depth, far_point)) return false;
    std::array<double, 5> depths{};
    depths.front() = perspective ? 0 : minimum_depth;
    depths.back() = maximum_depth;
    double near_distance = 0, far_distance = 0;
    double useful_near = 0;
    if (perspective) {
        near_distance = far_distance = camera_matrix[15];
        for (unsigned axis = 0; axis < 3; ++axis) {
            near_distance += camera_matrix[12+axis]*near_point[axis];
            far_distance += camera_matrix[12+axis]*far_point[axis];
        }
        if (!(near_distance > 0) || !(far_distance > near_distance)) return false;
        ShadowPoint first_receiver{};
        if (!shadow_unproject(inverse,0,0,minimum_depth,first_receiver)) return false;
        double receiver_distance=camera_matrix[15];
        for(unsigned axis=0;axis<3;++axis)
            receiver_distance+=camera_matrix[12+axis]*first_receiver[axis];
        // Clipping the occupied bounds includes their intersections with the
        // lower frustum edge, and also any nearer elevated receiver. Retreat
        // a quarter of that distance and round toward the camera, never past
        // occupied receivers. This is a quality policy, not a ground plane.
        useful_near=(std::max)(near_distance,std::floor(.75*receiver_distance/4)*4);
        const double fraction=(useful_near-near_distance)/(far_distance-near_distance);
        ShadowPoint start{};
        for(unsigned axis=0;axis<3;++axis)
            start[axis]=near_point[axis]+fraction*(far_point[axis]-near_point[axis]);
        depths.front()=shadow_clip_depth(camera_matrix,start);
    }
    for (unsigned index = 1; index < 4; ++index) {
        // The current camera-derived near bands supply quality scale in both
        // policies. Spatial allocation changes placement, not this sizing.
        const double fraction = perspective ? std::clamp(
            (useful_near+std::ldexp(near_distance, 3+index)-2*near_distance)/(far_distance-near_distance), 0.0, 1.0) :
            double(index)/4;
        ShadowPoint center{};
        for (unsigned axis = 0; axis < 3; ++axis)
            center[axis] = near_point[axis]+(far_point[axis]-near_point[axis])*fraction;
        depths[index] = shadow_clip_depth(camera_matrix, center);
    }

    ShadowPoint terrain_hit{};
    double ground_near=0,ground_far=0;
    const bool anchored=perspective && base_terrain && terrain_to_world &&
        base_terrain_quality_anchor(camera_matrix,*base_terrain,*terrain_to_world,base_terrain_generation,
            terrain_hit,ground_near,ground_far);
    if (anchored) {
        std::vector<NativeWorldBounds> clipped_receivers;
        try { clipped_receivers.resize(receiving_bounds.size()); }
        catch (...) { return false; }
        const bool spatial_ready=[&]() noexcept {
        // A visible base-terrain hit enables the spatial policy, but its exact
        // intersection can jump between slopes as the camera moves. Fit the
        // close maps to camera-depth bands instead; current receiver geometry
        // still supplies their depth and caster-contribution envelopes.
        ShadowPoint quality_far{};
        if (!shadow_unproject(inverse,0,0,1,quality_far)) return false;
        double quality_far_distance=camera_matrix[15];
        for(unsigned axis=0;axis<3;++axis)
            quality_far_distance+=camera_matrix[12+axis]*quality_far[axis];
        if (!(quality_far_distance>near_distance)) return false;
        // The close band comes from camera state only, never from the visible
        // terrain or receivers: its near start where the view's lower edge
        // meets the horizontal plane through the game camera's anchor (three
        // quarters of that distance, snapped down to 4 m: nearer ground above
        // the anchor plane still falls inside map 0), its far end a fixed
        // close range past that. Panning keeps both, so the maps' radii stay
        // and the cached layers scroll; only zoom, pitch or the anchor height
        // move them. The former terrain-fitted band (48-160 m from terrain
        // rays, 8 m steps, one step held) jumped 16 m and more while panning
        // and snapped whole regions across the close/far split (#196).
        double working_near=near_distance;
        if(std::isfinite(camera_anchor_z)) {
            double lowest=INFINITY;
            for(const double x:{-1.0,0.0,1.0}) {
                ShadowPoint from{},to{};
                if(!shadow_unproject(inverse,x,-1,0,from) || !shadow_unproject(inverse,x,-1,1,to))continue;
                const double rise=to[2]-from[2];
                if(!(rise<0))continue;            // the ray never descends to the plane
                const double t=(double(camera_anchor_z)-from[2])/rise;
                if(!(t>0 && t<=1))continue;
                double distance=camera_matrix[15];
                for(unsigned axis=0;axis<3;++axis)
                    distance+=camera_matrix[12+axis]*(from[axis]+t*(to[axis]-from[axis]));
                lowest=(std::min)(lowest,distance);
            }
            if(std::isfinite(lowest))working_near=(std::max)(near_distance,std::floor(.75*lowest/4)*4);
        }
        const double working_far=(std::min)(quality_far_distance,working_near+double(close_range));
        if (!(working_near>0 && working_far>working_near)) return false;
        const double focus_fraction=std::clamp(
            (working_far-near_distance)/(quality_far_distance-near_distance),0.0,1.0);
        std::array<double,2> center{};
        for(unsigned axis=0;axis<2;++axis)
            for(unsigned component=0;component<3;++component)
                center[axis]+=double(result.light_axes[axis][component])*
                    (near_point[component]+(quality_far[component]-near_point[component])*focus_fraction);
        std::array<double,4> quality_distances{working_near,0,0,working_far};
        std::array<double,4> quality_depths{0,0,0,0};
        for(unsigned index=0;index<4;++index){
            if(index>0 && index<3) {
                const double part=double(index)/3;
                const double logarithmic=working_near*std::pow(working_far/working_near,part);
                const double linear=working_near+(working_far-working_near)*part;
                quality_distances[index]=.5*(logarithmic+linear);
            }
            const double fraction=std::clamp((quality_distances[index]-near_distance)/
                (quality_far_distance-near_distance),0.0,1.0);
            ShadowPoint point{};
            for(unsigned axis=0;axis<3;++axis)
                point[axis]=near_point[axis]+(quality_far[axis]-near_point[axis])*fraction;
            quality_depths[index]=shadow_clip_depth(camera_matrix,point);
        }
        // Degenerate camera depth cannot size three ordered close bands.
        const bool quality_valid=quality_depths[0]<quality_depths[1] &&
            quality_depths[1]<quality_depths[2] && quality_depths[2]<quality_depths[3];
        if (quality_valid) for (std::size_t receiver_index=0;
            receiver_index<receiving_bounds.size();++receiver_index) {
            auto& clipped=clipped_receivers[receiver_index];
            const auto project=[&](const ShadowPoint& point) {
                std::array<float,3> lower{},upper{};
                for (unsigned axis=0;axis<3;++axis) {
                    double value=0;
                    for (unsigned component=0;component<3;++component)
                        value+=double(result.light_axes[axis][component])*point[component];
                    lower[axis]=next_float(static_cast<float>(value),-INFINITY);
                    upper[axis]=next_float(static_cast<float>(value),INFINITY);
                }
                include_world_point(clipped,lower);
                include_world_point(clipped,upper);
            };
            for(std::uint32_t k=camera_point_offsets[receiver_index];k<camera_point_offsets[receiver_index+1];++k)
                project(camera_points[k]);
        }
        if (quality_valid) for (unsigned index=0;index<5;++index) {
            auto& slice=result.slices[index];
            for (unsigned component=0;component<16;++component)
                slice.world_to_clip[component]=static_cast<float>(camera_matrix[component]);
            if (index<3) {
                RetainedDirectionalSlice quality{};
                quality.world_to_clip=slice.world_to_clip;
                // Measure the overlap in linear view distance. A fraction of
                // nonlinear clip depth pulled C1 nearly back to the camera.
                const unsigned first_active=3u-close_cascades;
                const double band_start=index==0||index==first_active?working_near:
                    quality_distances[index]-.35*(quality_distances[index]-quality_distances[index-1]);
                ShadowPoint start{};
                const double start_fraction=(band_start-near_distance)/
                    (quality_far_distance-near_distance);
                for(unsigned axis=0;axis<3;++axis)
                    start[axis]=near_point[axis]+start_fraction*(quality_far[axis]-near_point[axis]);
                const double first=(std::max)(0.0,double(next_float(
                    static_cast<float>(shadow_clip_depth(camera_matrix,start)),-INFINITY)));
                const double last=(std::min)(1.0,double(next_float(
                    static_cast<float>(quality_depths[index+1]),INFINITY)));
                for(unsigned column=0;column<4;++column)
                    quality.world_to_clip[8+column]=static_cast<float>((
                        camera_matrix[8+column]-first*camera_matrix[12+column])/(last-first));
                ShadowFrustum section{};
                if(!shadow_frustum(quality.world_to_clip,section)) return false;
                for(const auto& point:section.corners){
                    std::array<float,3> projected{};
                    for(unsigned axis=0;axis<3;++axis)
                        for(unsigned component=0;component<3;++component)
                            projected[axis]+=result.light_axes[axis][component]*
                                static_cast<float>(point[component]);
                    include_world_point(quality.receivers_in_light,projected);
                }
                if(!fit_shadow_camera_slice_xy(quality,section,result.light_axes,map_side)) return false;
                // Close cascades follow camera depth, so an approach can
                // only advance from C2 to C1 to C0. The fitted light center
                // covers the entire depth band independently of terrain rays.
                slice.world_to_clip=quality.world_to_clip;
                slice.light_radius=quality.light_radius;
                slice.light_center=quality.light_center;
                slice.blend_start_view_distance=static_cast<float>(
                    quality_distances[index+1]-.35*(quality_distances[index+1]-quality_distances[index]));
                slice.blend_end_view_distance=static_cast<float>(quality_distances[index+1]);
            } else if (index==3) {
                // Preserve the three near allocations and give the middle
                // distance its own stable grid. Remote receiver bounds must
                // not dilute the shadows visible around the camera.
                const double radius=4.0*result.slices[2].light_radius;
                if (!(radius>0) || !finite_value(radius)) return false;
                const double texel=2*radius/map_side;
                slice.light_radius=static_cast<float>(radius);
                for(unsigned axis=0;axis<2;++axis)
                    slice.light_center[axis]=static_cast<float>(
                        std::floor(center[axis]/texel+.5)*texel);
            }
            NativeWorldBounds occupied{};
            for (const auto& clipped:clipped_receivers) {
                if (!clipped.valid) continue;
                if (index<4 && (clipped.maximum[0]<slice.light_center[0]-slice.light_radius ||
                    clipped.minimum[0]>slice.light_center[0]+slice.light_radius ||
                    clipped.maximum[1]<slice.light_center[1]-slice.light_radius ||
                    clipped.minimum[1]>slice.light_center[1]+slice.light_radius)) continue;
                include_world_point(occupied,clipped.minimum);
                include_world_point(occupied,clipped.maximum);
            }
            if (!occupied.valid) continue;
            if (index<4) {
                // The complete map square admits upstream casters and supports
                // filter taps at its guarded selector boundary.
                slice.receivers_in_light=occupied;
                for (unsigned axis=0;axis<2;++axis) {
                    slice.receivers_in_light.minimum[axis]=slice.light_center[axis]-slice.light_radius;
                    slice.receivers_in_light.maximum[axis]=slice.light_center[axis]+slice.light_radius;
                }
            } else {
                slice.receivers_in_light=occupied;
                // The coarsest map contains the entire inner allocation even
                // after independent texel snapping and receiver fit.
                for (unsigned inner=0;inner<4;++inner) {
                    const auto& finer=result.slices[inner];
                    if (!finer.receivers_in_light.valid) continue;
                    for (unsigned axis=0;axis<2;++axis) {
                        slice.receivers_in_light.minimum[axis]=(std::min)(
                            slice.receivers_in_light.minimum[axis],
                            finer.light_center[axis]-finer.light_radius);
                        slice.receivers_in_light.maximum[axis]=(std::max)(
                            slice.receivers_in_light.maximum[axis],
                            finer.light_center[axis]+finer.light_radius);
                    }
                }
                // A distant receiver entering the camera may expand this map,
                // but leaving it must not collapse its scale by sixfold. The
                // captured tree view alternated between 200 and 1216 radius.
                if (!fit_shadow_receiver_xy(slice,map_side,
                    6.0*result.slices[3].light_radius)) return false;
            }
        }
        if(quality_valid) for(unsigned index=3;index<5;++index){
            auto& slice=result.slices[index];
            if(!slice.receivers_in_light.valid)continue;
            const double cell_side=2.0*slice.light_radius/far_receiver_grid_side;
            const double minimum_x=double(slice.light_center[0])-slice.light_radius;
            const double minimum_y=double(slice.light_center[1])-slice.light_radius;
            // Cover filter taps and cell-boundary roundoff without admitting the
            // entire map square. These are the actual camera-clipped receivers;
            // the larger bounds above still stabilize the shadow projection.
            const double padding=(std::max)(1.0,4.0*slice.light_radius/map_side);
            // The far envelope is the bounding box of every marked cell with the
            // height range of the receivers that marked them; per cell only the
            // bit and the index extremes are needed, edges are converted once.
            unsigned low_x=far_receiver_grid_side,low_y=far_receiver_grid_side,high_x=0,high_y=0;
            float low_z=INFINITY,high_z=-INFINITY;
            for(const auto& clipped:clipped_receivers){
                if(!clipped.valid)continue;
                std::array<unsigned,2> first{},last{};
                if(!far_receiver_cell_range(clipped,slice,padding,first,last))continue;
                bool marked=false;
                for(unsigned y=first[1];y<=last[1];++y)
                    for(unsigned x=first[0];x<=last[0];++x){
                        if(index==4 && result.slices[3].receivers_in_light.valid){
                            const double left=minimum_x+x*cell_side;
                            const double right=left+cell_side;
                            const double bottom=minimum_y+y*cell_side;
                            const double top=bottom+cell_side;
                            const auto& middle=result.slices[3];
                            const double core=.75*middle.light_radius;
                            // C3 owns this interior, including all near-map
                            // transitions. C4 is sampled at its outer blend
                            // band and beyond, so only that region needs depth.
                            if(left>=double(middle.light_center[0])-core &&
                               right<=double(middle.light_center[0])+core &&
                               bottom>=double(middle.light_center[1])-core &&
                               top<=double(middle.light_center[1])+core)continue;
                        }
                        const unsigned bit=y*far_receiver_grid_side+x;
                        slice.far_receiver_cells[bit/64]|=std::uint64_t{1}<<(bit%64);
                        low_x=(std::min)(low_x,x);high_x=(std::max)(high_x,x);
                        low_y=(std::min)(low_y,y);high_y=(std::max)(high_y,y);
                        marked=true;
                    }
                if(marked){low_z=(std::min)(low_z,clipped.minimum[2]);high_z=(std::max)(high_z,clipped.maximum[2]);}
            }
            if(low_x<=high_x) {
                const double left=minimum_x+low_x*cell_side,bottom=minimum_y+low_y*cell_side;
                const double right=minimum_x+high_x*cell_side+cell_side,top=minimum_y+high_y*cell_side+cell_side;
                include_world_point(slice.far_receivers_in_light,{next_float(static_cast<float>(left),-INFINITY),
                    next_float(static_cast<float>(bottom),-INFINITY),low_z});
                include_world_point(slice.far_receivers_in_light,{next_float(static_cast<float>(right),INFINITY),
                    next_float(static_cast<float>(top),INFINITY),high_z});
            }
        }
        return quality_valid;
        }();
        if (spatial_ready) {
            result.valid=true;
            result.spatial_selection=true;
            return true;
        }
        // Unsupported/degenerate quality placement retains the established
        // camera-fit selector. No partial spatial slice may escape.
        result.slices={};
    }

    // A planar/point receiver cannot supply four distinct float depth cuts.
    // One fitted map still covers it; its selector uses the original camera.
    const bool single = !perspective &&
        next_float(static_cast<float>(depths.front()), INFINITY) >= depths.back();
    for (unsigned index = 0; index < (single ? 1u : 4u); ++index) {
        if (!single && depths[index+1] <= depths[index]) continue;
        const double active_start=index==3u-close_cascades?depths.front():depths[index];
        if (perspective && (maximum_depth < active_start || minimum_depth > depths[index+1])) continue;
        auto& slice = result.slices[index];
        for (unsigned component = 0; component < 16; ++component)
            slice.world_to_clip[component] = static_cast<float>(camera_matrix[component]);
        if (!single) {
            const double first = (std::max)(0.f, next_float(static_cast<float>(active_start), -INFINITY));
            const double last = (std::min)(1.f, next_float(static_cast<float>(depths[index+1]), INFINITY));
            for (unsigned column = 0; column < 4; ++column)
                slice.world_to_clip[8+column] = static_cast<float>((camera_matrix[8+column]-
                    first*camera_matrix[12+column])/(last-first));
        }
        ShadowFrustum section{};
        if (!shadow_frustum(slice.world_to_clip, section)) return false;
        for (const auto& receiver : receiving_bounds) {
            shadow_receiver_points(receiver, section, [&](const ShadowPoint& point) {
                std::array<float, 3> lower{}, upper{};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    double value = 0;
                    for (unsigned component = 0; component < 3; ++component)
                        value += double(result.light_axes[axis][component])*point[component];
                    lower[axis] = next_float(static_cast<float>(value), -INFINITY);
                    upper[axis] = next_float(static_cast<float>(value), INFINITY);
                }
                include_world_point(slice.receivers_in_light, lower);
                include_world_point(slice.receivers_in_light, upper);
            });
        }
        if (perspective && index < 3) {
            if (!fit_shadow_camera_slice_xy(slice, section, result.light_axes,map_side)) return false;
        } else if (!fit_shadow_receiver_xy(slice,map_side)) return false;
    }
    result.valid = true;
    return true;
}

namespace {
#ifdef EID3D11_TESTING
std::uint64_t g_retained_shadow_projection_count=0;
#endif
}

std::uint8_t retained_world_light_contribution(const NativeWorldBounds& bounds,
    bool casts_shadow,const RetainedDirectionalCoverage& coverage,
    NativeWorldBounds& caster_in_light,float minimum_projected_texels) noexcept {
    caster_in_light={};
    if(!coverage.valid || !finite_world_bounds(bounds) || !casts_shadow || !coverage.directional)
        return 0;
    caster_in_light=light_bounds(bounds,coverage.light_axes);
#ifdef EID3D11_TESTING
    ++g_retained_shadow_projection_count;
#endif
    return retained_light_mask(caster_in_light,coverage,minimum_projected_texels);
}

std::uint8_t retained_light_mask(const NativeWorldBounds& caster_in_light,
    const RetainedDirectionalCoverage& coverage,float minimum_projected_texels) noexcept {
    if(!coverage.valid || !coverage.directional || !caster_in_light.valid)return 0;
    std::uint8_t mask=0;
    for (unsigned index = 0; index < 5; ++index) {
        const auto& slice=coverage.slices[index];
        const auto& receiver = slice.receivers_in_light;
        if (!receiver.valid || caster_in_light.minimum[2] > receiver.maximum[2]) continue;
        if (caster_in_light.maximum[0] < receiver.minimum[0] || caster_in_light.minimum[0] > receiver.maximum[0] ||
            caster_in_light.maximum[1] < receiver.minimum[1] || caster_in_light.minimum[1] > receiver.maximum[1]) continue;
        if(coverage.spatial_selection && index>=3 &&
           !far_receiver_cells_intersect(caster_in_light,slice))continue;
        // Camera-frustum culling would lose off-camera casters. Apply the
        // chosen size cutoff in each map's light-space texels instead.
        if(minimum_projected_texels>0) {
            const double minimum_extent=double(minimum_projected_texels)*
                (2.0*coverage.slices[index].light_radius/coverage.map_side);
            if(caster_in_light.maximum[0]-caster_in_light.minimum[0]<minimum_extent &&
               caster_in_light.maximum[1]-caster_in_light.minimum[1]<minimum_extent)continue;
        }
        mask |= static_cast<std::uint8_t>(1u<<index);
    }
    return mask;
}

NativeContribution retained_world_contribution(const NativeWorldBounds& bounds,
    bool casts_shadow, const RetainedDirectionalCoverage& coverage,
    NativeWorldBounds* caster_in_light,float minimum_projected_texels) noexcept {
    NativeContribution result{true, false, 0};
    if (!coverage.valid || !finite_world_bounds(bounds)) return result;
    result.color = retained_world_bounds_visible(bounds, coverage.world_to_clip);
    NativeWorldBounds ignored{};
    result.light_mask=retained_world_light_contribution(bounds,casts_shadow,coverage,
        caster_in_light ? *caster_in_light : ignored,minimum_projected_texels);
    return result;
}

#ifdef EID3D11_TESTING
void reset_retained_shadow_receiver_clip_count_for_test() noexcept {
    g_shadow_receiver_clip_count=0;
}
std::uint64_t retained_shadow_receiver_clip_count_for_test() noexcept {
    return g_shadow_receiver_clip_count;
}
void reset_retained_shadow_projection_count_for_test() noexcept {
    g_retained_shadow_projection_count=0;
}
std::uint64_t retained_shadow_projection_count_for_test() noexcept {
    return g_retained_shadow_projection_count;
}
#endif

bool build_static_shadow_summary(const std::vector<RetainedShadowCasterValue>& casters,
    const std::array<std::array<float,3>,3>& axes,std::uint64_t revision,
    StaticShadowSummary& summary) {
    summary=StaticShadowSummary{};summary.revision=revision;summary.axes=axes;
    std::vector<NativeWorldBounds> projected;projected.reserve(casters.size());
    summary.light.reserve(casters.size());
    std::array<float,2> low{INFINITY,INFINITY},high{-INFINITY,-INFINITY};
    for(const auto& caster:casters) {
        if(!finite_world_bounds(caster.bounds)){summary.light.push_back({});continue;}
        const auto value=light_bounds(caster.bounds,axes);
        summary.light.push_back(value);
        if(!value.valid)continue;
        for(unsigned axis=0;axis<2;++axis){
            low[axis]=(std::min)(low[axis],value.minimum[axis]);
            high[axis]=(std::max)(high[axis],value.maximum[axis]);
        }
        projected.push_back(value);
    }
    if(!projected.empty()) {
        constexpr unsigned side=128;
        const float extent=(std::max)(high[0]-low[0],high[1]-low[1]);
        summary.side=side;summary.origin=low;summary.cell=(std::max)(extent/side,1e-3f);
        summary.minimum_z.assign(std::size_t(side)*side,INFINITY);
        const auto cell_of=[&](float value,unsigned axis){
            return static_cast<unsigned>(std::clamp((value-low[axis])/summary.cell,0.f,float(side-1)));
        };
        for(const auto& value:projected)
            for(unsigned y=cell_of(value.minimum[1],1);y<=cell_of(value.maximum[1],1);++y)
                for(unsigned x=cell_of(value.minimum[0],0);x<=cell_of(value.maximum[0],0);++x){
                    auto& z=summary.minimum_z[std::size_t(y)*side+x];
                    z=(std::min)(z,value.minimum[2]);
                }
    }
    summary.valid=true;
    return true;
}

bool build_retained_shadow_frame(const RetainedDirectionalCoverage& coverage,
    const std::vector<RetainedShadowCasterValue>& casters,NativeShadowFrame& frame,
    const StaticShadowSummary* statics) noexcept {
    frame.publication_serial=0;
    frame.world_epoch=0;
    for(auto& slice:frame.slices){
        slice.enabled=false;slice.validity.clear();slice.static_validity.clear();
        slice.world_to_light={};slice.camera_slice={};slice.bias={};
        slice.receiver_minimum={};slice.receiver_maximum={};
    }
    if(!coverage.valid)return false;
    if(!coverage.directional){frame.publication_serial=next_retained_shadow_revision();return true;}
    try {
        // Each slice's validity lists, counted first so each is reserved once
        // (filled below in the same order).
        std::size_t list_sizes[5][2]{};   // [slice][terrain]
        for(const auto& caster:casters)
            for(unsigned index=0;index<5;++index)
                if(caster.mask&(1u<<index))++list_sizes[index][caster.revision.terrain?1:0];
        for(unsigned index=0;index<5;++index){
            const auto& source=coverage.slices[index];
            if(!source.receivers_in_light.valid)continue;
            if(!finite_value(source.light_radius)||source.light_radius<=0)return false;
            auto& slice=frame.slices[index];slice.enabled=true;slice.camera_slice=source.world_to_clip;
            slice.validity.reserve(list_sizes[index][0]);
            slice.static_validity.reserve(list_sizes[index][1]+(statics&&statics->valid?1u:0u));
            const auto& active_receivers=coverage.spatial_selection && index>=3 &&
                source.far_receivers_in_light.valid ? source.far_receivers_in_light :
                source.receivers_in_light;
            slice.receiver_minimum=active_receivers.minimum;
            slice.receiver_maximum=active_receivers.maximum;
            float minimum=source.receivers_in_light.minimum[2],maximum=source.receivers_in_light.maximum[2];
            for(const auto& caster:casters){
                if(!(caster.mask&(1u<<index)))continue;
                if(!finite_world_bounds(caster.bounds)||!finite_world_bounds(caster.light_bounds)||
                    !caster.revision.revision)return false;
                minimum=(std::min)(minimum,caster.light_bounds.minimum[2]);
                // Terrain belongs to the cached static layer.
                (caster.revision.terrain?slice.static_validity:slice.validity).push_back(caster.revision);
            }
            if(statics && statics->valid) {
                // The GPU culls persistent static casters per slice; here they
                // only extend depth upstream (grid cells under the slice) and
                // contribute one revision to the redraw decision.
                if(statics->side) {
                    const auto cell_of=[&](double value,unsigned axis){
                        return static_cast<unsigned>(std::clamp((value-statics->origin[axis])/statics->cell,
                            0.0,double(statics->side-1)));
                    };
                    const double radius=source.light_radius;
                    for(unsigned y=cell_of(source.light_center[1]-radius,1);y<=cell_of(source.light_center[1]+radius,1);++y)
                        for(unsigned x=cell_of(source.light_center[0]-radius,0);x<=cell_of(source.light_center[0]+radius,0);++x)
                            minimum=(std::min)(minimum,statics->minimum_z[std::size_t(y)*statics->side+x]);
                }
                NativeShadowCasterRevision summary{};
                summary.signed_id=INT32_MIN;
                summary.revision=statics->slice_revision[index]?statics->slice_revision[index]:statics->revision;
                slice.static_validity.push_back(summary);
            }
            // Receiver fitting owns XY and texel size. Contributing source
            // bounds extend only upstream depth, including off-camera casters.
            const float radius=source.light_radius;
            const float texel=2*radius/coverage.map_side;
            // World-anchored depth bins decouple the projection from exact
            // moving-caster extrema. Outward rounding and a guard preserve
            // upstream casters and receivers without retaining another cache.
            const double depth_guard=(std::max)(1.0,8.0*texel);
            // 16-unit bins: a pan admitting a slightly taller upstream caster
            // must not move the range (a moved range cannot be scrolled).
            constexpr double depth_step=16;
            minimum=static_cast<float>(std::floor((minimum-depth_guard)/depth_step)*depth_step);
            maximum=static_cast<float>(std::ceil((maximum+depth_guard)/depth_step)*depth_step);
            const float depth=maximum-minimum;if(!finite_value(depth)||depth<=0)return false;
            // The final slice's z lane selects guarded spatial coverage.
            // Close slices use their otherwise free z/w lanes for the linear
            // camera-distance overlap with the next slice.
            if(index<3 && coverage.spatial_selection &&
                (!(source.blend_start_view_distance>0) ||
                 !(source.blend_end_view_distance>source.blend_start_view_distance))) return false;
            slice.bias={texel,1/depth,
                index==4 && coverage.spatial_selection?1.f:source.blend_start_view_distance,
                source.blend_end_view_distance};
            for(unsigned axis=0;axis<3;++axis){
                for(unsigned component=0;component<3;++component)
                    slice.world_to_light[axis*4+component]=coverage.light_axes[axis][component]/(axis<2?radius:depth);
                slice.world_to_light[axis*4+3]=axis<2?-source.light_center[axis]/radius:-minimum/depth;
            }
            slice.world_to_light[15]=1;
            for(float value:slice.world_to_light)if(!finite_value(value))return false;
        }
        frame.slices[4].bias[2]=coverage.spatial_selection?1.f:0.f;
        frame.publication_serial=next_retained_shadow_revision();
        return true;
    }catch(...){frame={};return false;}
}

std::uint64_t next_retained_shadow_revision() noexcept {
    // A removed part can later reuse its ordinal within the same membership.
    // Process-unique value revisions prevent that replacement from aliasing a
    // previously accepted slice. This is a serial, never a content hash.
    static std::atomic<std::uint64_t> serial{0};
    auto value=serial.fetch_add(1,std::memory_order_relaxed)+1;
    if(!value)value=serial.fetch_add(1,std::memory_order_relaxed)+1;
    return value;
}

bool retained_visual_depth_revision(const RetainedVisualSample& sample,std::size_t index,
    double time,NativeShadowCasterRevision& revision) noexcept {
    revision={};
    if(!sample.assembly||!sample.prepared||index>=sample.assembly->parts.size()||
        index>=sample.prepared->parts.size())return false;
    const auto& assembly=*sample.assembly;const auto& part=assembly.parts[index];
    const auto& pose=sample.prepared->gpu_pose;
    if(!part.source_available||!part.geometry||!part.material||!pose.binding||
        (!part.surface&&!part.texture))return false;
    auto& stored=sample.prepared->parts[index];
    const auto same_resource=[](const ResourceIdentity& a,const ResourceIdentity& b){
        return a.container_incarnation==b.container_incarnation && a.absolute_offset==b.absolute_offset &&
            a.bytes==b.bytes && a.canonical_name==b.canonical_name;
    };
    bool same_sources=stored.depth_assembly && index<stored.depth_assembly->parts.size() && stored.depth_binding;
    if(same_sources && stored.depth_assembly!=sample.assembly){
        const auto& old=stored.depth_assembly->parts[index];
        same_sources=old.geometry && same_resource(old.geometry->identity,part.geometry->identity) &&
            old.skeleton_node==part.skeleton_node;
        if(same_sources && part.surface)
            same_sources=old.surface && old.surface->id==part.surface->id && old.surface->generation==part.surface->generation;
        else if(same_sources)
            same_sources=old.texture && old.texture->value_source==part.texture->value_source &&
                old.texture->value_serial==part.texture->value_serial && same_resource(old.texture->identity,part.texture->identity);
    }
    if(same_sources && stored.depth_binding!=pose.binding)
        same_sources=visual_skeleton_binding_matches(*stored.depth_binding,*assembly.skeleton,
            sample.selection,false);
    VisualPreparedResources::DepthPose value{};
    value.root=pose.root_to_world;value.shape=pose.shape;value.center=pose.center_shift;
    value.frame=pose.frame;value.fraction=pose.fraction;value.height=pose.height_scale;
    value.alpha=retained_visual_effective_alpha(sample,part);
    if(!retained_visual_texture_shift(part.texture_type,time,value.u,value.v))return false;
    if(!same_sources || !stored.depth_revision || !(stored.depth_pose==value)){
        stored.depth_revision=next_retained_shadow_revision();stored.depth_pose=value;
    }
    stored.depth_assembly=sample.assembly;stored.depth_binding=pose.binding;
    revision={sample.lifecycle.id,static_cast<UINT>(index),sample.lifecycle.generation,
        sample.lifecycle.positive_world_epoch,sample.resource.serial,stored.depth_revision,false};
    return true;
}

bool retained_figure_position_bounds(const FigureCpuAsset& figure,
    const std::array<float, 3>& shape, NativeWorldBounds& bounds) noexcept {
    bounds = {};
    if (!figure.position_groups || (figure.shape_count != 1 && figure.shape_count != 8) ||
        figure.positions.size() != std::uint64_t(figure.position_groups) * figure.shape_count)
        return false;
    for (std::size_t group = 0; group < figure.position_groups; ++group) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            for (unsigned axis = 0; axis < 3; ++axis) {
                float endpoints[8]{};
                for (unsigned endpoint = 0; endpoint < figure.shape_count; ++endpoint) {
                    const auto& packed = figure.positions[endpoint * figure.position_groups + group];
                    endpoints[endpoint] = axis == 0 ? packed.x[lane] :
                        axis == 1 ? packed.y[lane] : packed.z[lane];
                }
                const float value = figure.shape_count == 1 ? endpoints[0] : select_endpoint(endpoints, shape);
                if (!finite_value(value)) { bounds = {}; return false; }
                if (!bounds.valid) bounds.minimum[axis] = bounds.maximum[axis] = value;
                else {
                    bounds.minimum[axis] = (std::min)(bounds.minimum[axis], value);
                    bounds.maximum[axis] = (std::max)(bounds.maximum[axis], value);
                }
            }
            bounds.valid = true;
        }
    }
    return true;
}

bool retained_visual_part_visible(const RetainedVisualSample& sample,
    const VisualPartAsset& part, bool draw_undetected_units) noexcept {
    if (part.explicitly_hidden || (sample.values.unit && unit_service_node(part.name.c_str())) ||
        !retained_visual_sample_shown(sample, draw_undetected_units)) return false;
    return !(part.weapon && sample.visibility.hide_weapons) &&
           !(part.arrow && sample.visibility.hide_arrow);
}

float retained_visual_effective_alpha(const RetainedVisualSample& sample,
    const VisualPartAsset& part) noexcept {
    // Default-color figures use authored packed alpha; their material/effect
    // alpha is not read by the packed GPU color path.
    if (part.environment_material && part.material) {
        const auto& material=*part.environment_material;
        const float factor=material.alpha==0?1.f:material.alpha_factor;
        return std::clamp(part.material->diffuse[3]*sample.values.fade_alpha*factor,0.f,1.f);
    }
    if (part.figure_flags & 0x100u) return float(part.diffuse>>24)/255.f;
    if (!part.material) return 0;
    // A completed host material already includes fade/invisibility. Do not
    // replace that current value with the independent retail effect state.
    if (sample.completed_pose) return std::clamp(part.material->diffuse[3],0.f,1.f);
    // A unit's native material alpha is the game's fade/invisibility channel
    // (it writes fade steps into it and stops at 14/15), not appearance data:
    // units are opaque unless invisible (pulse) or fading (fade value).
    if (sample.values.unit)
        return sample.values.invisible || sample.values.fade_alpha < 1 ? sample.effects.opacity : 1.f;
    return std::clamp(part.material->diffuse[3],0.f,1.f);
}

bool retained_visual_texture_shift(std::uint32_t type, double time,
    float& u, float& v) noexcept {
    u = v = 0;
    if (!finite_value(time) || time < 0 || time > INT_MAX) return false;
    if (type != 13 && type != 14 && type != 16) return true;
    const auto columns = type == 16 ? 8u : 4u;
    const auto rows = type == 13 ? 2u : 4u;
    const auto frame = static_cast<unsigned>(time) % (columns * rows);
    u = static_cast<float>(frame % columns) / columns;
    const auto row = frame / columns;
    v = type == 13 ? static_cast<float>(row) / rows : 1.0f - static_cast<float>(row) / rows;
    return true;
}

#ifdef EID3D11_TESTING
bool retained_visual_nodes(const RetainedVisualSample& sample,
    std::vector<VisualNodeSample>& nodes) {
    nodes.clear();
    if (!sample.assembly || !sample.assembly->skeleton) return false;
    return sample_visual_skeleton(*sample.assembly->skeleton,sample.selection,
        retained_visual_shape(sample),sample.assembly->center_shift,sample.pose.phase,nodes,false);
}

#endif

namespace {

bool same_prepared_resource(const ResourceIdentity& a,
    const ResourceIdentity& b) noexcept {
    return a.container_incarnation==b.container_incarnation &&
        a.absolute_offset==b.absolute_offset && a.bytes==b.bytes &&
        a.canonical_name==b.canonical_name;
}

bool same_prepared_selection(
    const VisualPreparedResources::SelectionEnvelope& cached,
    const VisualClipSelection& current) noexcept {
    if (cached.neutral || current.neutral)
        return cached.neutral==current.neutral;
    if (cached.ordinal!=current.ordinal || !current.database || !current.clip)
        return false;
    const auto database=cached.database.lock();
    const auto clip=cached.clip.lock();
    return database && clip &&
        same_prepared_resource(database->identity,current.database->identity) &&
        same_prepared_resource(clip->identity,current.clip->identity);
}

bool prepare_visual_selection_envelopes(const RetainedVisualSample& sample) {
    if (!sample.assembly || !sample.assembly->skeleton || !sample.prepared)
        return false;
    const auto& assembly=*sample.assembly;
    auto& prepared=*sample.prepared;
    if (!prepared.skeleton_binding || !visual_skeleton_binding_matches(
        *prepared.skeleton_binding,*assembly.skeleton,sample.selection,false)) {
        auto binding=std::make_shared<VisualSkeletonBinding>();
        if (!bind_visual_skeleton(assembly.skeleton,sample.selection,false,*binding))
            return false;
        prepared.skeleton_binding=std::move(binding);
    }
    if (prepared.envelope_binding!=prepared.skeleton_binding ||
        prepared.envelope_shape!=retained_visual_shape(sample) ||
        prepared.envelope_center!=assembly.center_shift) {
        if (!visual_skeleton_envelopes(*prepared.skeleton_binding,
            retained_visual_shape(sample),assembly.center_shift,
            prepared.node_envelopes)) return false;
        prepared.envelope_binding=prepared.skeleton_binding;
        prepared.envelope_shape=retained_visual_shape(sample);
        prepared.envelope_center=assembly.center_shift;
    }
    if (prepared.parts.size()!=assembly.parts.size())
        prepared.parts.resize(assembly.parts.size());
    return true;
}

bool prepare_visual_part_envelope(const RetainedVisualSample& sample,
    std::size_t index) noexcept {
    if (!sample.assembly || !sample.prepared ||
        index>=sample.assembly->parts.size() ||
        index>=sample.prepared->parts.size()) return false;
    const auto& assembly=*sample.assembly;
    const auto& part=assembly.parts[index];
    auto& prepared=*sample.prepared;
    if (!part.geometry || part.skeleton_node>=prepared.node_envelopes.size() ||
        !prepared.skeleton_binding) return false;
    auto& target=prepared.parts[index];
    if (target.envelope_assembly==sample.assembly &&
        target.bound_skeleton==prepared.skeleton_binding &&
        target.envelope_shape==retained_visual_shape(sample)) return true;
    const auto& figure=*part.geometry;
    if ((figure.shape_count!=1 && figure.shape_count!=8) ||
        figure.shape_position_radii.size()<figure.shape_count) return false;
    double radius=0;
    for (unsigned endpoint=0;endpoint<figure.shape_count;++endpoint) {
        double weight=1;
        if (figure.shape_count==8) for (unsigned axis=0;axis<3;++axis)
            weight*=endpoint&(1u<<axis)?retained_visual_shape(sample)[axis]:
                1.0-retained_visual_shape(sample)[axis];
        radius+=std::abs(weight)*figure.shape_position_radii[endpoint];
    }
    if (const auto* track=prepared.skeleton_binding->tracks[part.skeleton_node])
        radius+=track->morph_radius;
    radius+=astral_margin(assembly,part);   // astral veil sway and clump drift
    const auto& envelope=prepared.node_envelopes[part.skeleton_node];
    target.geometry_radius=next_float(static_cast<float>(radius),INFINITY);
    target.envelope_radius=next_float(static_cast<float>(
        envelope.origin_radius+envelope.rotation_scale*radius),INFINITY);
    target.envelope_center=envelope.center;
    if (!finite_value(target.envelope_radius) ||
        !finite_value(target.geometry_radius)) return false;
    target.envelope_assembly=sample.assembly;
    target.bound_skeleton=prepared.skeleton_binding;
    target.envelope_shape=retained_visual_shape(sample);
    return true;
}

bool retained_visual_selection_envelope_radius(
    const RetainedVisualSample& sample, float& radius) noexcept {
    radius=0;
    if (!sample.values.unit || !sample.assembly || !sample.prepared)
        return false;
    try {
        const auto& assembly=*sample.assembly;
        auto& prepared=*sample.prepared;
        const auto& shape=retained_visual_shape(sample);
        const bool same_epoch=
            prepared.selection_envelope_resource.signed_id==sample.resource.signed_id &&
            prepared.selection_envelope_resource.serial==sample.resource.serial &&
            prepared.selection_envelope_assembly==sample.assembly &&
            prepared.selection_envelope_shape==shape &&
            prepared.selection_envelope_center==assembly.center_shift &&
            prepared.selection_envelope_scale==assembly.scale;
        if (!same_epoch) {
            prepared.selection_envelope_resource=sample.resource;
            prepared.selection_envelope_assembly=sample.assembly;
            prepared.selection_envelope_shape=shape;
            prepared.selection_envelope_center=assembly.center_shift;
            prepared.selection_envelope_scale=assembly.scale;
            prepared.selection_envelopes.clear();
            prepared.selection_envelope_replace=0;
        }
        for (const auto& cached:prepared.selection_envelopes)
            if (same_prepared_selection(cached,sample.selection)) {
                radius=cached.radius;
                return finite_value(radius) && radius>=0;
            }
#ifdef EID3D11_TESTING
        if (++prepared.selection_envelope_builds==0)
            ++prepared.selection_envelope_builds;
#endif
        VisualSkeletonBinding binding;
        if (!bind_visual_skeleton(assembly.skeleton,sample.selection,false,binding))
            return false;
        std::vector<VisualNodeEnvelope> node_envelopes;
        if (!visual_skeleton_envelopes(binding,shape,assembly.center_shift,
            node_envelopes) || node_envelopes.size()!=assembly.skeleton->nodes.size())
            return false;
        double local_radius=0;
        for (std::size_t index=0;index<assembly.parts.size();++index) {
            const auto& part=assembly.parts[index];
            if (!part.geometry || part.skeleton_node>=node_envelopes.size())
                return false;
            const auto& figure=*part.geometry;
            if ((figure.shape_count!=1 && figure.shape_count!=8) ||
                figure.shape_position_radii.size()<figure.shape_count)
                return false;
            double geometry_radius=0;
            for (unsigned endpoint=0;endpoint<figure.shape_count;++endpoint) {
                double weight=1;
                if (figure.shape_count==8) for (unsigned axis=0;axis<3;++axis)
                    weight*=endpoint&(1u<<axis)?shape[axis]:1.0-shape[axis];
                geometry_radius+=std::abs(weight)*figure.shape_position_radii[endpoint];
            }
            if (const auto* track=binding.tracks[part.skeleton_node])
                geometry_radius+=track->morph_radius;
            const auto& envelope=node_envelopes[part.skeleton_node];
            double squared=0;
            for (float coordinate:envelope.center)
                squared+=double(coordinate)*coordinate;
            const double part_radius=envelope.origin_radius+
                double(envelope.rotation_scale)*geometry_radius;
            const double complete=std::sqrt(squared)+part_radius;
            if (!finite_value(complete)) return false;
            local_radius=(std::max)(local_radius,complete);
        }
        double scale=0;
        for (float axis:assembly.scale) {
            if (!finite_value(axis)) return false;
            scale=(std::max)(scale,std::abs(double(axis)));
        }
        const double complete=local_radius*scale;
        if (!finite_value(complete) ||
            complete>(std::numeric_limits<float>::max)()) return false;
        radius=next_float(static_cast<float>(complete),INFINITY);
        if (!finite_value(radius) || radius<0) return false;
        VisualPreparedResources::SelectionEnvelope value;
        value.database=sample.selection.database;
        value.clip=sample.selection.clip;
        value.ordinal=sample.selection.ordinal;
        value.radius=radius;
        value.neutral=sample.selection.neutral;
        if (prepared.selection_envelopes.size()<kVisualQueueLimit)
            prepared.selection_envelopes.push_back(std::move(value));
        else {
            prepared.selection_envelopes[prepared.selection_envelope_replace]=
                std::move(value);
            prepared.selection_envelope_replace=
                (prepared.selection_envelope_replace+1)%kVisualQueueLimit;
        }
        return true;
    } catch (...) {
        radius=0;
        return false;
    }
}

} // namespace

#include "gltf_submission.inl"
#include "skm_submission.inl"

bool retained_visual_gpu_pose(const RetainedVisualSample& sample,
    NativeWorldGeometry::Pose*& pose) {
    pose=nullptr;
    if (!sample.assembly || !sample.assembly->skeleton || !sample.prepared) return false;
    const auto& assembly=*sample.assembly;
    auto& prepared=*sample.prepared;
    const bool object_pose = !sample.values.unit || sample.completed_pose;
    if (!prepare_visual_selection_envelopes(sample)) return false;
    auto& current=prepared.gpu_pose;
    VisualFramePhase phase{};
    if (!visual_frame_phase(sample.pose.phase,prepared.skeleton_binding->maximum_frame,phase)) return false;
    std::array<float,16> root{};
    if (object_pose) {
        if (!retained_object_matrix(sample.object_pose.position,sample.object_pose.quaternion,root)) return false;
        for (unsigned row=0;row<3;++row) for (unsigned column=0;column<3;++column)
            root[row*4+column]*=assembly.scale[column];
    } else if (!visual_node_world_matrix(sample.pose,VisualNodeSample{},assembly.scale,root)) return false;
    double height=1;
    if (!sample.selection.neutral) {
        const auto& h=sample.selection.database->heights;
        if (h[1]!=0) height=(double(h[0])+retained_visual_shape(sample)[2]*(double(h[2])-h[0]))/h[1];
    }
    if (!finite_value(height) || std::abs(height)>(std::numeric_limits<float>::max)()) return false;
    current.binding=prepared.skeleton_binding;
    current.owner=sample.prepared;
    current.root_to_world=root;current.shape=retained_visual_shape(sample);current.center_shift=assembly.center_shift;
    current.frame=phase.frame;current.fraction=phase.fraction;current.height_scale=static_cast<float>(height);
    current.contact_world_height=0;current.physique_sole={};
    // A glTF body owns its contact envelope and GPU pose. Attachment queries
    // need only socket ancestors; do not evaluate every legacy FIG node here.
    if(sample.values.unit) try {
        bool present=false;
        const auto gltf=skn::select_gltf(assembly.skeleton->visual_reference,assembly.main_hero_visual,present);
        if(present) {
            if(!prepare_gltf_pose(sample,gltf))return false;
            pose=&current;return true;
        }
    } catch(const std::exception& error) {
        skn::report_rejection(assembly.skeleton->visual_reference,error.what());return false;
    }
    if(object_pose){
        // Use the same selected skeleton pose and FIG boxes as the owned geometry,
        // without copying vertex or morph payloads. Pose-key reuse skips this work.
        std::vector<VisualNodeSample> nodes;
        if(!sample_visual_skeleton(*assembly.skeleton,sample.selection,current.shape,
            assembly.center_shift,sample.pose.phase,nodes,false))return false;
        float lo=FLT_MAX,hi=-FLT_MAX;
        VisualPoseState local_root;
        local_root.tilted_facing=local_root.root_rotation_valid=true;
        local_root.root_rotation={1,0,0,0};
        std::array<float,16> placement{};
        if(!retained_object_matrix(sample.object_pose.position,sample.object_pose.quaternion,placement))return false;
        for(const auto& part:assembly.parts){
            if(!part.source_available||part.explicitly_hidden||!part.geometry)continue;
            if(part.skeleton_node>=nodes.size())return false;
            // A part without FIG boxes adds no height: skip it rather than drop the
            // whole pose (the object would vanish). With no boxed part at all the
            // band falls back to the F10 maximum (contactBandHeight caps it).
            if(part.geometry->bounds.empty())continue;
            const auto box=part.geometry->shape_count==1?part.geometry->bounds.front():
                selected_bounds(*part.geometry,current.shape);
            std::array<float,16> local{};
            if(!visual_node_world_matrix(local_root,nodes[part.skeleton_node],assembly.scale,local))return false;
            const auto z=contact_world_vertical_bounds(box.center,box.minimum,box.maximum,
                retained_multiply_matrix(placement,local));
            lo=(std::min)(lo,z[0]);hi=(std::max)(hi,z[1]);
        }
        current.contact_world_height=hi>=lo?hi-lo:FLT_MAX;
    }
    if(!prepare_baked_motion(sample))return false;
    pose=&current;
    return true;
}

int retained_visual_equipment_point(const RetainedVisualSample& sample,const char* slot,
    const char* anchor,std::array<float,3>& point) noexcept try {
    if(!sample.assembly||!sample.assembly->skeleton||!sample.prepared)return 0;
    bool present=false;
    const auto asset=skn::select_gltf(sample.assembly->skeleton->visual_reference,
        sample.assembly->main_hero_visual,present);
    if(!present||!asset->gltf_asset)return 0;
    NativeWorldGeometry::Pose* pose=nullptr;
    if(!retained_visual_gpu_pose(sample,pose)||!pose)return -1;
    const auto& body=*asset->gltf_asset;
    std::string socket=std::string(slot)=="hand_l"?"socket_hand_l":"socket_hand_r";
    std::array<float,3> local{};
    for(const auto& item:prepare_gltf_equipment(sample,body).items) if(item.slot==slot) {
        const auto found=item.asset->anchors.find(anchor);
        if(found!=item.asset->anchors.end()) {
            socket=item.socket;local=found->second;
            for(unsigned c=0;c<3;++c)local[c]+=item.offset[c];
            break;
        }
    }
    const gltf::Playback playback{pose->gltf_clip,pose->gltf_previous,pose->gltf_time,
        pose->gltf_previous_time,pose->gltf_blend};
    auto matrix=gltf::multiply(pose->root_to_world,
        gltf::socket_pose(*body.animation,body.attachment_chain(socket),playback,
            {1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1},sample.prepared->motion_physique.get()));
    // The grip solver may move the primary socket while retaining the weapon
    // in its hand. Effects must use the same final world matrix as skinning.
    if(pose->overrides && pose->binding) {
        const auto joint=std::find_if(body.geometry.joints.begin(),body.geometry.joints.end(),
            [&](const skn::Joint& item){return item.name==socket;});
        if(joint!=body.geometry.joints.end()) {
            const auto index=pose->binding->gltf_node_base+unsigned(joint-body.geometry.joints.begin());
            const auto found=std::find_if(pose->overrides->begin(),pose->overrides->end(),
                [&](const auto& item){return item.node==index;});
            if(found!=pose->overrides->end())
                for(unsigned r=0;r<3;++r)std::copy_n(found->value.rows[r].begin(),4,matrix.data()+r*4);
        }
    }
    for(unsigned r=0;r<3;++r) {
        point[r]=matrix[r*4+3];
        for(unsigned c=0;c<3;++c)point[r]+=matrix[r*4+c]*local[c];
        if(!std::isfinite(point[r]))return -1;
    }
    return 1;
} catch(const std::exception& error) {
    skn::report_rejection(sample.assembly?sample.assembly->skeleton->visual_reference:"",error.what());
    return -1;
}

bool retained_visual_combat_anchor(const RetainedVisualSample& sample,
    std::array<float,3>& anchor) {
    if (!sample.values.unit || !sample.assembly || !sample.prepared) return false;
    const auto& assembly=*sample.assembly;
    auto& prepared=*sample.prepared;
    if (prepared.combat_anchor_assembly.lock()!=sample.assembly) {
        prepared.combat_anchor_assembly=sample.assembly;
        prepared.combat_anchor_valid=false;
        prepared.combat_anchor_bodies.clear();
        std::vector<VisualNodeSample> nodes;
        VisualClipSelection reference{}; reference.neutral=true;
        if (!sample_visual_skeleton(*assembly.skeleton,reference,assembly.shape,
            {},0,nodes,false)) return false;
        std::vector<std::uint32_t> roots(nodes.size());
        for (std::uint32_t i=0;i<nodes.size();++i) {
            const auto parent=assembly.skeleton->links[i].parent_index;
            if (parent!=UINT_MAX && parent>=i) return false;
            roots[i]=parent==UINT_MAX ? i : roots[parent];
        }
        for (const auto& part:assembly.parts) {
            // The plate measures the body. Animated weapon/arrow admission and
            // animation channels cannot change this reference size.
            if (part.weapon || part.arrow || !part.source_available || part.explicitly_hidden ||
                !part.geometry || part.geometry->bounds.empty()) continue;
            if (part.skeleton_node>=nodes.size()) return false;
            const auto box=part.geometry->shape_count==1 ? part.geometry->bounds.front() :
                selected_bounds(*part.geometry,assembly.shape);
            const auto root=roots[part.skeleton_node];
            auto found=std::find_if(prepared.combat_anchor_bodies.begin(),prepared.combat_anchor_bodies.end(),
                [root](const auto& body) { return body.root==root; });
            if (found==prepared.combat_anchor_bodies.end()) {
                prepared.combat_anchor_bodies.push_back({root,{FLT_MAX,FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX,-FLT_MAX}});
                found=prepared.combat_anchor_bodies.end()-1;
            }
            for (unsigned axis=0;axis<3;++axis) {
                const float base=nodes[part.skeleton_node].translation[axis]+box.center[axis];
                const float lo=base+box.minimum[axis],hi=base+box.maximum[axis];
                if (!finite_value(lo) || !finite_value(hi) || lo>hi) return false;
                found->minimum[axis]=(std::min)(found->minimum[axis],lo);
                found->maximum[axis]=(std::max)(found->maximum[axis],hi);
            }
        }
        if (prepared.combat_anchor_bodies.empty()) return false;
        prepared.combat_anchor_valid=true;
    }
    if (!prepared.combat_anchor_valid) return false;
    // This current owned root contains actor placement/orientation and scale.
    // Reference geometry was measured unscaled, so neither is applied twice.
    const auto& pose=prepared.gpu_pose;
    if (!prepared.skeleton_binding) return false;
    VisualPoseState outer;
    outer.tilted_facing=outer.root_rotation_valid=true;
    std::array<float,3> minimum{FLT_MAX,FLT_MAX,FLT_MAX},maximum{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for (const auto& body:prepared.combat_anchor_bodies) {
        if (body.root>=prepared.skeleton_binding->tracks.size()) return false;
        VisualNodeSample body_root;
        if (!sample_visual_track(prepared.skeleton_binding->tracks[body.root],
            double(pose.frame)+pose.fraction,body_root,false)) return false;
        // Match the owned shader: raw ANM quaternion, scaled root translation,
        // then center shift, then actor placement/assembly scale. Limb tracks
        // never resize this body volume. Multiple effective roots stay separate.
        for (unsigned axis=0;axis<3;++axis)
            body_root.translation[axis]=body_root.translation[axis]*pose.height_scale-pose.center_shift[axis];
        std::array<float,16> local{};
        if (!visual_node_world_matrix(outer,body_root,{1,1,1},local)) return false;
        const auto world=retained_multiply_matrix(pose.root_to_world,local);
        for (unsigned axis=0;axis<3;++axis) {
            float lo=world[axis*4+3],hi=lo;
            for (unsigned lane=0;lane<3;++lane) {
                const float a=world[axis*4+lane]*body.minimum[lane],b=world[axis*4+lane]*body.maximum[lane];
                lo+=(std::min)(a,b); hi+=(std::max)(a,b);
            }
            minimum[axis]=(std::min)(minimum[axis],lo);
            maximum[axis]=(std::max)(maximum[axis],hi);
        }
    }
    anchor={(minimum[0]+maximum[0])*.5f,(minimum[1]+maximum[1])*.5f,maximum[2]};
    return finite_value(anchor[0]) && finite_value(anchor[1]) && finite_value(anchor[2]);
}

bool retained_visual_neutral_parts_box(const RetainedVisualSample& sample,
    const std::vector<std::size_t>& parts,std::array<float,3>& minimum,std::array<float,3>& maximum) {
    if (!sample.assembly||!sample.assembly->skeleton) return false;
    const auto& assembly=*sample.assembly;
    VisualClipSelection neutral{}; neutral.neutral=true;
    const auto& shape=retained_visual_shape(sample);
    std::vector<VisualNodeSample> nodes;
    if (!sample_visual_skeleton(*assembly.skeleton,neutral,shape,assembly.center_shift,0,nodes,false)) return false;
    VisualPoseState root;
    root.tilted_facing=root.root_rotation_valid=true;
    root.root_rotation={1,0,0,0};
    minimum={FLT_MAX,FLT_MAX,FLT_MAX}; maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for (const auto index:parts) {
        if (index>=assembly.parts.size()) return false;
        const auto& part=assembly.parts[index];
        if (!part.geometry||part.geometry->bounds.empty()||part.skeleton_node>=nodes.size()) continue;
        const auto box=part.geometry->shape_count==1?part.geometry->bounds.front():selected_bounds(*part.geometry,shape);
        std::array<float,16> local{};
        if (!visual_node_world_matrix(root,nodes[part.skeleton_node],{1,1,1},local)) return false;
        for (unsigned corner=0;corner<8;++corner) {
            std::array<float,3> p{};
            for (unsigned axis=0;axis<3;++axis)
                p[axis]=(corner>>axis)&1u?box.maximum[axis]:box.minimum[axis];   // FIG boxes are absolute
            for (unsigned axis=0;axis<3;++axis) {
                const float value=local[axis*4+3]+local[axis*4]*p[0]+local[axis*4+1]*p[1]+local[axis*4+2]*p[2];
                minimum[axis]=(std::min)(minimum[axis],value); maximum[axis]=(std::max)(maximum[axis],value);
            }
        }
    }
    return minimum[0]<=maximum[0]&&minimum[1]<=maximum[1]&&minimum[2]<=maximum[2]&&
        finite_value(minimum[0])&&finite_value(maximum[2]);
}

bool retained_visual_part_bounds(const RetainedVisualSample& sample,std::size_t index,NativeWorldBounds& bounds) {
    // Selection remains semantic/gameplay driven; only the culling envelope
    // changes. Using the whole skin envelope per part is conservative.
    bool skn_present=false;
    try{if(!skn_pose_bounds(sample,bounds,skn_present))return false;if(skn_present)return true;}
    catch(const std::exception& error){skn::report_rejection(sample.assembly->skeleton->visual_reference,error.what());return false;}
    bounds={};
    if (!sample.assembly||!sample.prepared||index>=sample.prepared->parts.size()) return false;
    auto& prepared=*sample.prepared;
    if (!prepare_visual_part_envelope(sample,index)) return false;
    auto& target=prepared.parts[index];
    const auto& root=prepared.gpu_pose.root_to_world;
    for (unsigned axis=0;axis<3;++axis) {
        double center=root[axis*4+3],squared=0;
        for (unsigned column=0;column<3;++column) {
            const double value=root[axis*4+column];center+=value*target.envelope_center[column];squared+=value*value;
        }
        const double extent=target.envelope_radius*std::sqrt(squared);
        bounds.minimum[axis]=next_float(static_cast<float>(center-extent),-INFINITY);
        bounds.maximum[axis]=next_float(static_cast<float>(center+extent),INFINITY);
    }
    bounds.valid=true;return finite_world_bounds(bounds);
}

// Fixed-function state of every owned unit/object figure record.
void retained_visual_part_fixed_state(const RetainedVisualSample& sample,NativeOwnedFixedState& state) noexcept {
    state={};state.enabled=true;
    state.cull_mode=D3DCULL_CW;state.fill_mode=D3DFILL_SOLID;
    state.z_enable=D3DZB_TRUE;state.z_write_enable=TRUE;state.z_func=D3DCMP_LESSEQUAL;
    state.alpha_blend_enable=TRUE;state.source_blend=D3DBLEND_SRCALPHA;state.destination_blend=D3DBLEND_INVSRCALPHA;
    state.alpha_test_enable=TRUE;state.alpha_func=D3DCMP_GREATER;state.alpha_ref=sample.values.unit?10:200;
    state.texture_factor=0xffffffffu;state.color_op=state.alpha_op=D3DTOP_MODULATE;
    state.color_arg1=state.alpha_arg1=D3DTA_TEXTURE;state.color_arg2=state.alpha_arg2=D3DTA_DIFFUSE;
    state.address_u=state.address_v=D3DTADDRESS_WRAP;state.min_filter=state.mag_filter=D3DTFG_LINEAR;
}

// Per-frame record values of one part (fade, highlight, stun, fog, scroll).
bool retained_visual_part_values(const RetainedVisualSample& sample,const VisualPartAsset& part,
    double time,std::uint8_t fog,NativePreparedPartValues& values) {
    values={};
    values.ordering=static_cast<UINT>(retained_visual_preserve_color_order(sample,part)?
        NativeOrdering::preserve:NativeOrdering::derive_from_state);
    const auto current_fog=(sample.values.object_flags&0x800u)?255:fog;
    const bool defaults=(part.figure_flags&0x100u)!=0,specular=(part.material_flags&0x10u)!=0||sample.values.stunned;
    values.flags=(defaults?4u:0u)|(specular?8u:0u);
    values.default_diffuse=defaults?part.diffuse:0xffffffffu;values.default_specular=defaults?part.specular:UINT(current_fog)<<24;
    values.lighting_enabled=defaults?0:1;
    std::copy_n(part.material->diffuse.begin(),3,values.diffuse.begin());
    values.alpha=retained_visual_effective_alpha(sample,part);
    values.emissive=retained_visual_emissive(sample.values,sample.quest_light_active,*part.material);values.specular=sample.values.stunned?std::array<float,4>{sample.effects.stun_red,0,0,1}:part.material->specular;
    values.specular[3]=1;values.specular_enable=specular?TRUE:FALSE;
    values.flags|=retained_visual_sway_flags(sample,part);
    // Astral rift clumps glow in the atlas colours: unlit, white default diffuse.
    const unsigned astral=sample.assembly?astral_kind(*sample.assembly,part):0u;
    if (astral) values.flags|=astral_record_flags(*sample.assembly,part);
    if (astral==2) { values.lighting_enabled=0; values.default_diffuse=0xffffffffu; values.flags|=4u; }
    // Rigid world objects meet the terrain with its colour; units never do
    // (nor the floating astral veils and clumps).
    if (!sample.values.unit) { if (!astral) values.flags |= native_ground_contact_flag; }
    // Units carry the unit flag: snow build-up gives them a sheen only.
    else if(sample.values.unit) values.flags |= native_unit_flag;
    if (sample.assembly && sample.assembly->sways && !sample.values.unit &&
        retained_visual_part_sway(part).foliage)
        values.flags |= native_foliage_part_flag;
    // Fur prototype, strands: a unit's hair part keeps its alpha here (its sun
    // shadow); only its colour record is hidden (fur_strand_hair_record).
    return retained_visual_texture_shift(part.texture_type,time,values.u_shift,values.v_shift);
}

PartSwayEvidence retained_visual_figure_sway(const FigureCpuAsset& figure,
    const TextureAlphaPixels& alpha) noexcept {
    PartSwayEvidence result;
    if (!alpha.width || !alpha.height) return result;
    std::vector<std::uint8_t> clear_vertex(figure.components.size(), 0);
    const auto texel = [&](double coordinate, std::uint32_t size) {
        const double wrapped = coordinate - std::floor(coordinate);   // WRAP addressing
        return (std::min)(static_cast<std::uint32_t>(wrapped * size), size - 1);
    };
    for (std::size_t t = 0; t + 2 < figure.indices.size(); t += 3) {
        std::array<NativeTextureCoordinate, 3> uv{};
        for (unsigned k = 0; k < 3; ++k) {
            const auto component = figure.indices[t + k];
            if (component >= figure.components.size()) return {};
            const auto index = figure.components[component].texture;
            if (index >= figure.texture_coordinates.size()) return {};
            uv[k] = figure.texture_coordinates[index];
        }
        // 45 interior barycentric samples: dense enough that a trunk strip
        // never reports a cut-out edge and a leaf card always does.
        bool cut = false;
        for (unsigned i = 0; i < 9 && !cut; ++i)
            for (unsigned j = 0; j < 9 - i && !cut; ++j) {
                const double a = (i + .5) / 9.5, b = (j + .5) / 9.5, c = 1 - a - b;
                cut = texture_alpha_texel_cut(alpha,
                    texel(a * uv[0].u + b * uv[1].u + c * uv[2].u, alpha.width),
                    texel(a * uv[0].v + b * uv[1].v + c * uv[2].v, alpha.height));
            }
        if (cut) result.foliage = true;
        else for (unsigned k = 0; k < 3; ++k) clear_vertex[figure.indices[t + k]] = 1;
    }
    for (std::size_t c = 0; c < figure.components.size(); ++c)
        if (clear_vertex[c]) result.clear_positions.push_back(figure.components[c].position);
    std::sort(result.clear_positions.begin(), result.clear_positions.end());
    result.clear_positions.erase(std::unique(result.clear_positions.begin(), result.clear_positions.end()),
        result.clear_positions.end());
    return result;
}

TrunkShape retained_visual_trunk_shape(const FigureCpuAsset& figure,
    const std::vector<std::uint16_t>& clear_positions, const std::array<float,3>& shape) noexcept {
    if ((figure.shape_count != 1 && figure.shape_count != 8) ||
        figure.positions.size() < std::size_t(figure.shape_count) * figure.position_groups) return {};
    const auto position = [&](std::uint32_t index) {
        std::array<double,3> sum{};
        for (std::uint32_t endpoint = 0; endpoint < figure.shape_count; ++endpoint) {
            double weight = 1;
            if (figure.shape_count == 8) for (unsigned axis = 0; axis < 3; ++axis)
                weight *= (endpoint & (1u << axis)) ? shape[axis] : 1.0 - shape[axis];
            const auto& group = figure.positions[std::size_t(endpoint) * figure.position_groups + index / 4];
            sum[0] += weight * group.x[index % 4]; sum[1] += weight * group.y[index % 4];
            sum[2] += weight * group.z[index % 4];
        }
        return sum;
    };
    double low = (std::numeric_limits<double>::max)(), high = -low;
    for (const auto& component : figure.components) {
        if (component.position >= figure.position_groups * 4u) return {};
        const double z = position(component.position)[2];
        low = (std::min)(low, z); high = (std::max)(high, z);
    }
    if (figure.components.empty()) return {};
    const double lower = low + .4 * (high - low);
    double radius = 0, top = 0;
    for (const auto index : clear_positions) {
        if (index >= figure.position_groups * 4u) return {};
        const auto p = position(index);
        if (p[2] <= lower) radius = (std::max)(radius, std::sqrt(p[0] * p[0] + p[1] * p[1]));
        top = (std::max)(top, p[2]);
    }
    return {static_cast<float>(radius), static_cast<float>(top), static_cast<float>((std::max)(high, 0.0))};
}

PartSwayEvidence retained_visual_part_sway(const VisualPartAsset& part) noexcept {
    if(part.environment_material) return {part.environment_foliage,{}};
    if (!part.geometry || !(part.surface || part.texture)) return {};
    try {
        // Shared figure and texture values classify once per session, not
        // once per placed tree.
        const std::string key = part.geometry->identity.key() + '|' + (part.surface ?
            "s" + std::to_string(part.surface->id) + ':' + std::to_string(part.surface->generation) :
            "t" + part.texture->identity.key() + ':' + std::to_string(part.texture->value_serial));
        static std::mutex lock;
        static std::unordered_map<std::string, PartSwayEvidence> classes;
        {
            std::lock_guard<std::mutex> guard(lock);
            const auto found = classes.find(key);
            if (found != classes.end()) return found->second;
        }
        TextureAlphaPixels alpha;
        const bool readable = part.surface ? visual_surface_alpha_pixels(*part.surface, alpha) :
            texture_alpha_pixels(*part.texture, 0, alpha);
        const auto value = readable ? retained_visual_figure_sway(*part.geometry, alpha) : PartSwayEvidence{};
        std::lock_guard<std::mutex> guard(lock);
        classes.emplace(key, value);
        return value;
    } catch (...) { return {}; }
}

std::uint32_t retained_visual_sway_shape(const TrunkShape& trunk,
    const std::array<float,3>& scale) noexcept {
    const float across = (std::max)(std::fabs(scale[0]), std::fabs(scale[1]));
    const float up = std::fabs(scale[2]);
    const auto code = [](float value, float steps) {
        const float rounded = std::ceil(value * steps);
        return finite_value(rounded) ? static_cast<std::uint32_t>((std::min)((std::max)(rounded, 0.f), 63.f)) : 63u;
    };
    return (code(trunk.top * up, 4) << 10) | (code(trunk.radius * across, 16) << 16) |
        ((std::max)(code(trunk.height * up, 4), 1u) << 22);
}

std::uint32_t retained_visual_sway_flags(const RetainedVisualSample& sample,
    const VisualPartAsset& part) noexcept {
    if (!sample.assembly || !sample.assembly->sways || sample.values.unit ||
        part.skeleton_node < sample.assembly->sway_root ||
        part.skeleton_node - sample.assembly->sway_root > native_sway_root_max) return 0;
    return native_sway_flag | sample.assembly->sway_shape |
        native_sway_root_bits(part.skeleton_node - sample.assembly->sway_root);
}

namespace {
std::mutex g_active_statics_lock;
std::shared_ptr<const ActiveStaticsIndex> g_active_statics_index;
std::uint64_t g_active_statics_revision = 0;
std::atomic<std::uint64_t> g_active_statics_failures{0};
std::atomic<std::uint32_t> g_active_statics_admitted{0};
std::shared_ptr<const ActiveStaticsCasters> g_active_statics_casters;
std::uint64_t g_active_statics_caster_revision = 0;
std::mutex g_active_statics_refusal_lock;
ActiveStaticsRefusals g_active_statics_refusals;
}
void active_statics_casters_publish(std::vector<std::int32_t> ids) noexcept try {
    auto value = std::make_shared<ActiveStaticsCasters>();
    value->ids = std::move(ids);
    std::lock_guard<std::mutex> guard(g_active_statics_lock);
    value->revision = ++g_active_statics_caster_revision;
    g_active_statics_casters = std::move(value);
} catch (...) {}
std::shared_ptr<const ActiveStaticsCasters> active_statics_casters() noexcept {
    std::lock_guard<std::mutex> guard(g_active_statics_lock);
    return g_active_statics_casters;
}
namespace {
std::mutex g_active_statics_request_lock;
std::vector<ActiveStaticsModelRequest> g_active_statics_requests;
std::unordered_set<std::uint64_t> g_active_statics_requested;
}
void active_statics_index_publish(std::shared_ptr<ActiveStaticsIndex> index) noexcept try {
    if (!index) {
        // Released: the next catalog requests its models afresh.
        std::lock_guard<std::mutex> guard(g_active_statics_request_lock);
        g_active_statics_requests.clear(); g_active_statics_requested.clear();
        std::lock_guard<std::mutex> refusals(g_active_statics_refusal_lock);
        g_active_statics_refusals = {};
    }
    std::lock_guard<std::mutex> guard(g_active_statics_lock);
    if (!index) index = std::make_shared<ActiveStaticsIndex>();
    index->revision = ++g_active_statics_revision;
    g_active_statics_index = std::move(index);
} catch (...) {}
void active_statics_request(const RetainedVisualSample& sample, std::uint64_t key) noexcept try {
    std::lock_guard<std::mutex> guard(g_active_statics_request_lock);
    if (!g_active_statics_requested.insert(key).second) return;
    g_active_statics_requests.push_back({key, sample.assembly, retained_visual_shape(sample)});
} catch (...) {}
std::vector<ActiveStaticsModelRequest> active_statics_take_requests() noexcept {
    std::vector<ActiveStaticsModelRequest> taken;
    std::lock_guard<std::mutex> guard(g_active_statics_request_lock);
    taken.swap(g_active_statics_requests);
    return taken;
}
namespace {
// A part's texture by content: the game texture's name, the same whether the
// part draws the archive .mmp or the game's own surface of it.
std::string retained_visual_texture_content_name(const VisualPartAsset& part) {
    std::string name = part.texture_source_name.empty() ? part.material_name : part.texture_source_name;
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
}
std::uint64_t retained_visual_object_space_model(const RetainedVisualSample& sample) noexcept try {
    if (!sample.assembly || !sample.prepared) return 0;
    const auto& shape = retained_visual_shape(sample);
    auto& cache = sample.prepared->object_space_model;
    if (cache.assembly == sample.assembly.get() && cache.revision == sample.assembly->revision &&
        std::memcmp(cache.shape.data(), shape.data(), sizeof(float) * 3) == 0) return cache.key;
    const auto& assembly = *sample.assembly;
    // Content identity only: the model resource, each part's FIG resource and
    // its texture's name. Never asset pointers or game surface ids: the same
    // model arrives with an archive texture after a direct load and with the
    // game's own surface (one per object) after travel, and pointer keys gave
    // every object its own catalog model and impostor (zone13 after zone12:
    // 1174 models / 1112 impostors instead of 102 / 12, houses missing).
    std::uint64_t key = 1469598103934665603ull;
    if (assembly.skeleton) {
        key = hash_string(key, assembly.skeleton->visual_reference);
        key = hash_string(key, assembly.skeleton->model_container.key());
    }
    key = hash_bytes(key, shape.data(), sizeof(float) * 3);
    key = hash_bytes(key, assembly.scale.data(), sizeof(float) * 3);
    key = hash_bytes(key, assembly.center_shift.data(), sizeof(float) * 3);
    for (const auto& part : assembly.parts) {
        key = hash_string(key, part.geometry ? part.geometry->identity.key() : std::string());
        key = hash_string(key, retained_visual_texture_content_name(part));
        const std::uint32_t values[2]{part.skeleton_node, (part.explicitly_hidden ? 1u : 0u) | (part.source_available ? 2u : 0u)};
        key = hash_bytes(key, values, sizeof(values));
    }
    if (!key) key = 1;
    cache.assembly = sample.assembly.get(); cache.revision = assembly.revision; cache.key = key; cache.shape = shape;
    return key;
} catch (...) {
    return 0;
}
bool prepare_live_static_candidate(const ActiveStaticsModelRequest& request,
    StaticObjectSource& source, std::int32_t model_index,
    std::shared_ptr<RetainedStaticSourceCandidate>& prepared) noexcept try {
    prepared.reset();
    if (!request.assembly || !request.assembly->skeleton) return false;
    const auto& assembly = *request.assembly;
    source.shape = request.shape; source.position = {}; source.quaternion = {1, 0, 0, 0};
    auto candidate = std::make_shared<RetainedStaticSourceCandidate>();
    candidate->signed_id = model_index;
    candidate->source = &source;
    candidate->assembly = request.assembly;
    // The neutral node placement retained_visual_object_space_parts
    // evaluates for the live object, without its world placement.
    std::vector<VisualNodeSample> nodes;
    VisualClipSelection neutral{}; neutral.neutral = true;
    if (!sample_visual_skeleton(*assembly.skeleton, neutral, request.shape, assembly.center_shift, 0, nodes, false)) return false;
    VisualPoseState local_root;
    local_root.tilted_facing = local_root.root_rotation_valid = true;
    local_root.root_rotation = {1, 0, 0, 0};
    const auto finite = [](float value) { return finite_value(value); };
    const auto node_local = [&](std::uint32_t node, std::array<float, 16>& local) {
        return node < nodes.size() && visual_node_world_matrix(local_root, nodes[node], assembly.scale, local) &&
            std::all_of(local.begin(), local.end(), finite);
    };
    candidate->nodes.resize(assembly.parts.size());
    bool drawn = false;
    for (std::size_t index = 0; index < assembly.parts.size(); ++index) {
        const auto& part = assembly.parts[index];
        auto& node = candidate->nodes[index];
        if (!part.geometry || part.explicitly_hidden || !part.source_available) continue;
        const auto& figure = *part.geometry;
        if (!node_local(part.skeleton_node, node.object_local)) return false;
        node.object_to_world = node.object_local;
        if (figure.shape_count == 8 && figure.bounds.size() == 8) {
            node.bounds = selected_bounds(figure, request.shape);
            if (!selected_groups(figure.positions, figure.position_groups, figure.shape_count, request.shape, node.positions)) return false;
        } else if (figure.shape_count == 1 && !figure.bounds.empty() &&
                   figure.positions.size() == figure.position_groups) {
            node.bounds = figure.bounds.front(); node.positions = figure.positions;
        } else return false;
        node.figure = &figure;node.environment_material=part.environment_material;
        node.normals=figure.normals;
        // The bake reads the texture the posed records made resident: an
        // archive member under the same cache key, or the game surface by its
        // id and generation. A generated CPU texture has no bake input yet.
        const auto* texture = part.texture.get();
        if (part.surface) {
            // The impostor key names the texture the surface was created
            // from, never the surface id: objects drawing the same texture
            // through their own surfaces (zone13 after travel: one per
            // object) share one key and one bake.
            node.surface = part.surface;
            node.texture_name = retained_visual_texture_content_name(part);
            const std::uint64_t identity = hash_string(1469598103934665603ull, "texture:" + node.texture_name);
            node.texture_identity = identity ? identity : 1u;
        } else if (texture && texture->value_source == TextureValueSource::archive && texture->source &&
            !texture->mips.empty()) {
            node.texture = texture;
            std::uint64_t identity = hash_string(1469598103934665603ull, texture->identity.key());
            identity = hash_string(identity, part.texture_source_name);
            node.texture_identity = identity ? identity : 1u;
            node.texture_format = texture_format(texture->format);
            for(const auto& mip:texture->mips)node.texture_mips.push_back({texture->source->data()+mip.byte_offset,mip.width,mip.height,mip.bytes});
        } else candidate->impostor_inputs_complete = false;
        std::uint64_t geometry = hash_string(1469598103934665603ull, figure.identity.key());
        geometry = hash_bytes(geometry, request.shape.data(), sizeof(float) * 3);
        node.geometry_identity = folded_identity(geometry);
        drawn = true;
    }
    if (!drawn) return false;
    if (assembly.sways) {
        std::array<float, 16> root{};
        if (node_local(assembly.sway_root, root)) {
            candidate->root_world = {root[3], root[7], root[11]};
            candidate->root_valid = true;
        }
    }
    candidate->prepared = true;
    prepared = std::move(candidate);
    return true;
} catch (...) {
    prepared.reset();
    return false;
}
std::shared_ptr<const ActiveStaticsIndex> active_statics_index() noexcept {
    std::lock_guard<std::mutex> guard(g_active_statics_lock);
    return g_active_statics_index;
}
std::uint64_t active_statics_failures() noexcept { return g_active_statics_failures.load(); }
void active_statics_note_failure() noexcept { ++g_active_statics_failures; }
const char* object_space_refusal_name(ObjectSpaceRefusal step) noexcept {
    static const char* const names[] = {"pose", "part_prepare", "part_submission", "place_node", "place_bounds",
        "invalid", "model_missing", "part_source", "node_unmatched", "figure_mismatch", "queue", "adopt", "exception"};
    static_assert(sizeof(names) / sizeof(names[0]) == std::size_t(ObjectSpaceRefusal::count), "refusal names");
    return step < ObjectSpaceRefusal::count ? names[std::size_t(step)] : "?";
}
void active_statics_note_refusal(ObjectSpaceRefusal step, const char* detail) noexcept try {
    if (step >= ObjectSpaceRefusal::count) return;
    std::lock_guard<std::mutex> guard(g_active_statics_refusal_lock);
    auto& r = g_active_statics_refusals;
    ++r.counts[std::size_t(step)]; ++r.revision;
    auto& examples = r.examples[std::size_t(step)];
    if (detail && examples.size() < 3) examples.emplace_back(detail);
} catch (...) {}
ActiveStaticsRefusals active_statics_refusals() noexcept try {
    std::lock_guard<std::mutex> guard(g_active_statics_refusal_lock);
    return g_active_statics_refusals;
} catch (...) { return {}; }
void active_statics_note_admitted(std::uint32_t objects) noexcept { g_active_statics_admitted = objects; }
std::uint32_t active_statics_admitted() noexcept { return g_active_statics_admitted.load(); }

ObjectSpaceReason retained_visual_object_space_reason(const RetainedVisualSample& sample,
    const ActiveStaticsIndex* index, const std::vector<std::int32_t>& time_active_ids, std::uint64_t& model) noexcept {
    model = 0;
    if (!index || !sample.assembly || !sample.prepared || !sample.assembly->skeleton ||
        !index->eligible.count(sample.lifecycle.id)) return ObjectSpaceReason::not_eligible;
    if (sample.values.unit) return ObjectSpaceReason::unit;
    if (sample.values.hidden || sample.values.dead || sample.values.invisible) return ObjectSpaceReason::hidden;
    if (!sample.selection.neutral ||
        std::binary_search(time_active_ids.begin(), time_active_ids.end(), sample.lifecycle.id))
        return ObjectSpaceReason::animating;
    // Unit scale and no centre shift, as every catalog model so far.
    for (unsigned axis = 0; axis < 3; ++axis)
        if (sample.assembly->scale[axis] != 1.f || sample.assembly->center_shift[axis] != 0.f)
            return ObjectSpaceReason::scaled;
    for (const auto& part : sample.assembly->parts)
        if (part.texture_type == 13 || part.texture_type == 14 || part.texture_type == 16 ||
            retained_visual_preserve_color_order(sample, part)) return ObjectSpaceReason::texture_kind;
    model = retained_visual_object_space_model(sample);
    if (!model || index->failed.count(model)) return ObjectSpaceReason::model_failed;
    if (!index->models.count(model)) return ObjectSpaceReason::model_pending;
    const auto& refused = sample.prepared->object_space_refused;
    if (refused.assembly == sample.assembly.get() && refused.revision == sample.assembly->revision &&
        refused.index == index->revision) return ObjectSpaceReason::refused;
    return ObjectSpaceReason::admitted;
}

bool retained_visual_object_space_wanted(const RetainedVisualSample& sample,
    const ActiveStaticsIndex* index, const std::vector<std::int32_t>& time_active_ids, std::uint64_t& model) noexcept {
    const auto reason = retained_visual_object_space_reason(sample, index, time_active_ids, model);
    return reason == ObjectSpaceReason::admitted || reason == ObjectSpaceReason::model_pending ||
        reason == ObjectSpaceReason::refused;
}

namespace {
std::array<std::atomic<std::uint32_t>, 2 * std::size_t(ObjectSpaceReason::count)> g_object_space_reasons{};
}
void active_statics_reasons_publish(const std::array<std::uint32_t, 2 * std::size_t(ObjectSpaceReason::count)>& counts) noexcept {
    for (std::size_t k = 0; k < counts.size(); ++k) g_object_space_reasons[k] = counts[k];
}
std::array<std::uint32_t, 2 * std::size_t(ObjectSpaceReason::count)> active_statics_reasons() noexcept {
    std::array<std::uint32_t, 2 * std::size_t(ObjectSpaceReason::count)> counts{};
    for (std::size_t k = 0; k < counts.size(); ++k) counts[k] = g_object_space_reasons[k];
    return counts;
}

bool retained_visual_object_space_admitted(const RetainedVisualSample& sample,
    const ActiveStaticsIndex* index, const std::vector<std::int32_t>& time_active_ids) noexcept {
    std::uint64_t model = 0;
    return retained_visual_object_space_reason(sample, index, time_active_ids, model) == ObjectSpaceReason::admitted;
}

bool retained_visual_object_space_parts(const RetainedVisualSample& sample,
    const std::uint32_t* parts, std::size_t count, const NativeWorldBounds* posed_bounds,
    std::vector<std::array<float, 16>>& node_to_world, std::vector<std::uint32_t>& sway_flags,
    std::array<float, 3>& sway_root) {
    node_to_world.clear(); sway_flags.clear(); sway_root = {};
    // Each false is one refused registration (active_statics_note_refusal).
    char detail[256];
    const auto refuse = [&](ObjectSpaceRefusal step, const char* text) {
        active_statics_note_refusal(step, text); return false; };
    const auto node_failed = [&](const char* what, std::size_t part) {
        std::snprintf(detail, sizeof(detail), "object=%d %s part=%zu", sample.lifecycle.id, what, part);
        return refuse(ObjectSpaceRefusal::place_node, detail); };
    if (!sample.assembly || !sample.assembly->skeleton) return node_failed("no_skeleton", 0);
    const auto& assembly = *sample.assembly;
    // retained_visual_gpu_pose's object placement and neutral node samples
    // (its contact span evaluates the parts the same way).
    std::vector<VisualNodeSample> nodes;
    VisualClipSelection neutral{}; neutral.neutral = true;
    const auto& shape = retained_visual_shape(sample);
    if (!sample_visual_skeleton(*assembly.skeleton, neutral, shape, assembly.center_shift, 0, nodes, false)) return node_failed("skeleton_sample", 0);
    std::array<float, 16> placement{};
    if (!retained_object_matrix(sample.object_pose.position, sample.object_pose.quaternion, placement)) return node_failed("object_matrix", 0);
    VisualPoseState local_root;
    local_root.tilted_facing = local_root.root_rotation_valid = true;
    local_root.root_rotation = {1, 0, 0, 0};
    const auto node_world = [&](std::uint32_t node, std::array<float, 16>& world) {
        std::array<float, 16> local{};
        if (node >= nodes.size() || !visual_node_world_matrix(local_root, nodes[node], assembly.scale, local)) return false;
        world = retained_multiply_matrix(placement, local);
        return std::all_of(world.begin(), world.end(), [](float v) { return std::isfinite(v); });
    };
    if (assembly.sways) {
        std::array<float, 16> root{};
        if (!node_world(assembly.sway_root, root)) return node_failed("sway_root", 0);
        sway_root = {root[3], root[7], root[11]};
    }
    node_to_world.resize(count); sway_flags.resize(count);
    for (std::size_t k = 0; k < count; ++k) {
        if (parts[k] >= assembly.parts.size()) return node_failed("part_index", k);
        const auto& part = assembly.parts[parts[k]];
        if (!part.geometry || !node_world(part.skeleton_node, node_to_world[k])) return node_failed("node_matrix", parts[k]);
        sway_flags[k] = retained_object_space_sway_flags(assembly, part);
        // Self-check: every vertex of the part at this placement lies in its
        // posed bounds, else the placement disagrees. The vertices are within
        // the shape-weighted position radius of the node origin (the radius
        // the posed envelope is built from, prepare_visual_part_envelope);
        // the authored FIG box is no bound of the complection-blended
        // positions and refused agreeing placements (0.07-0.11 m outside).
        const auto& figure = *part.geometry;
        if (posed_bounds && posed_bounds[k].valid && (figure.shape_count == 1 || figure.shape_count == 8) &&
            figure.shape_position_radii.size() >= figure.shape_count) {
            double radius = 0;
            for (unsigned endpoint = 0; endpoint < figure.shape_count; ++endpoint) {
                double weight = 1;
                if (figure.shape_count == 8) for (unsigned axis = 0; axis < 3; ++axis)
                    weight *= endpoint & (1u << axis) ? shape[axis] : 1.0 - shape[axis];
                radius += std::abs(weight) * figure.shape_position_radii[endpoint];
            }
            const auto& m = node_to_world[k];
            for (unsigned axis = 0; axis < 3; ++axis) {
                const double row = std::sqrt(double(m[axis * 4]) * m[axis * 4] + double(m[axis * 4 + 1]) * m[axis * 4 + 1] +
                    double(m[axis * 4 + 2]) * m[axis * 4 + 2]);
                const double origin = m[axis * 4 + 3], extent = radius * row;
                const double slack = 0.05 + 1e-3 * std::abs(origin);
                if (origin - extent < posed_bounds[k].minimum[axis] - slack || origin + extent > posed_bounds[k].maximum[axis] + slack) {
                    std::snprintf(detail, sizeof(detail),
                        "object=%d part=%u node=%u axis=%u origin=%.3f radius=%.3f posed=[%.3f,%.3f] shapes=%u",
                        sample.lifecycle.id, parts[k], part.skeleton_node, axis, origin, extent,
                        posed_bounds[k].minimum[axis], posed_bounds[k].maximum[axis], unsigned(figure.shape_count));
                    return refuse(ObjectSpaceRefusal::place_bounds, detail);
                }
            }
        }
    }
    return true;
}

std::uint32_t retained_object_space_sway_flags(const VisualAssemblyAsset& assembly,
    const VisualPartAsset& part) noexcept {
    if (!assembly.sways || part.skeleton_node < assembly.sway_root ||
        part.skeleton_node - assembly.sway_root > native_sway_root_max) return 0;
    return native_sway_flag | assembly.sway_shape;
}

void sway_tree_assembly(VisualAssemblyAsset& assembly) noexcept {
    // GLB wind dimensions were resolved together with its geometry.
    if (assembly.environment_source || !assembly.skeleton) return;
    const auto& links = assembly.skeleton->links;
    std::uint32_t roots = 0, root = 0;
    for (std::uint32_t i = 0; i < links.size(); ++i)
        if (links[i].parent_index == UINT_MAX && links[i].parent.empty()) { ++roots; root = i; }
    if (roots != 1) return;
    bool foliage = false;
    TrunkShape trunk;
    for (const auto& part : assembly.parts) {
        const auto evidence = retained_visual_part_sway(part);
        foliage = foliage || evidence.foliage;
        if (part.skeleton_node == root && part.geometry) {
            const auto value = retained_visual_trunk_shape(*part.geometry, evidence.clear_positions, assembly.shape);
            trunk.radius = (std::max)(trunk.radius, value.radius);
            trunk.top = (std::max)(trunk.top, value.top);
            trunk.height = (std::max)(trunk.height, value.height);
        }
    }
    assembly.sways = foliage;
    assembly.sway_shape = retained_visual_sway_shape(trunk, assembly.scale);
    assembly.sway_root = root;
}

namespace {
// A figure vertex in its node space at the unit's own shape (the 8-shape
// trilinear blend the geometry uses, select_endpoint; shape 0 for a
// single-shape figure). Only positions the components reference: padding
// lanes hold no vertex. The shapes differ widely (tiger bd: radius 0.455 at
// shapes 0-3, 1.259 at 4-7), so sizes from shape 0 alone are wrong.
bool fur_position(const FigureCpuAsset& figure, const std::array<float,3>& shape, std::uint32_t index, float (&v)[3]) noexcept {
    const std::size_t groups = figure.position_groups, group = index >> 2;
    if (index >= groups * 4u) return false;
    const unsigned lane = index & 3u;
    const bool blended = figure.shape_count == 8 && figure.positions.size() >= groups * 8u;
    if (!blended) {
        if (group >= figure.positions.size()) return false;
        const auto& g = figure.positions[group];
        v[0] = g.x[lane]; v[1] = g.y[lane]; v[2] = g.z[lane];
    } else {
        float x[8], y[8], z[8];
        for (std::size_t k = 0; k < 8; ++k) {
            const auto& g = figure.positions[k * groups + group];
            x[k] = g.x[lane]; y[k] = g.y[lane]; z[k] = g.z[lane];
        }
        v[0] = select_endpoint(x, shape); v[1] = select_endpoint(y, shape); v[2] = select_endpoint(z, shape);
    }
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
// A figure's vertex box in its node space at the unit's shape.
bool fur_figure_box(const FigureCpuAsset& figure, const std::array<float,3>& shape, float (&lo)[3], float (&hi)[3]) noexcept {
    for (unsigned a = 0; a < 3; ++a) { lo[a] = FLT_MAX; hi[a] = -FLT_MAX; }
    bool any = false;
    for (const auto& component : figure.components) {
        float v[3];
        if (!fur_position(figure, shape, component.position, v)) continue;
        for (unsigned a = 0; a < 3; ++a) { lo[a] = (std::min)(lo[a], v[a]); hi[a] = (std::max)(hi[a], v[a]); }
        any = true;
    }
    return any;
}
// A figure's vertex sphere in its node space (centre, radius) at the unit's
// shape, from the decoded positions: the authored FIG bounds can be empty or
// zero (menu run 2026-10-29: every human head and the tiger read radius 0).
bool fur_figure_sphere(const FigureCpuAsset& figure, const std::array<float,3>& shape, std::array<float,4>& sphere) noexcept {
    sphere = {};
    float lo[3], hi[3];
    if (!fur_figure_box(figure, shape, lo, hi)) return false;
    for (unsigned a = 0; a < 3; ++a) sphere[a] = (lo[a] + hi[a]) * .5f;
    float r2 = 0;
    for (const auto& component : figure.components) {
        float v[3];
        if (!fur_position(figure, shape, component.position, v)) continue;
        const float d[3]{v[0] - sphere[0], v[1] - sphere[1], v[2] - sphere[2]};
        r2 = (std::max)(r2, d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    }
    sphere[3] = std::sqrt(r2);
    return sphere[3] > 0;
}
// The hair mesh's strand axis (FurProfile::hair_axis): the UV direction
// whose surface tangent is closer to vertical, weighted by triangle area.
std::uint32_t fur_hair_axis(const FigureCpuAsset& figure) noexcept {
    const std::size_t groups = figure.position_groups;
    double vertical[2]{};
    const auto position = [&](std::uint32_t index, double (&v)[3]) {
        const std::size_t group = index >> 2;
        if (index >= groups * 4u || group >= figure.positions.size()) return false;
        const auto& g = figure.positions[group]; const unsigned lane = index & 3u;
        v[0] = g.x[lane]; v[1] = g.y[lane]; v[2] = g.z[lane];
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    };
    for (std::size_t t = 0; t + 2 < figure.indices.size(); t += 3) {
        double p[3][3], uv[3][2]; bool ok = true;
        for (unsigned k = 0; k < 3 && ok; ++k) {
            const auto ci = figure.indices[t + k];
            if (ci >= figure.components.size()) { ok = false; break; }
            const auto& c = figure.components[ci];
            ok = position(c.position, p[k]) && c.texture < figure.texture_coordinates.size();
            if (ok) { uv[k][0] = figure.texture_coordinates[c.texture].u; uv[k][1] = figure.texture_coordinates[c.texture].v; }
        }
        if (!ok) continue;
        double e1[3], e2[3];
        for (unsigned d = 0; d < 3; ++d) { e1[d] = p[1][d] - p[0][d]; e2[d] = p[2][d] - p[0][d]; }
        const double d1u = uv[1][0] - uv[0][0], d1v = uv[1][1] - uv[0][1], d2u = uv[2][0] - uv[0][0], d2v = uv[2][1] - uv[0][1];
        const double det = d1u * d2v - d2u * d1v;
        if (std::abs(det) < 1e-12) continue;
        const double cx = e1[1]*e2[2] - e1[2]*e2[1], cy = e1[2]*e2[0] - e1[0]*e2[2], cz = e1[0]*e2[1] - e1[1]*e2[0];
        const double area = .5 * std::sqrt(cx*cx + cy*cy + cz*cz);
        for (unsigned axis = 0; axis < 2; ++axis) {
            double tangent[3];
            for (unsigned d = 0; d < 3; ++d)
                tangent[d] = axis == 0 ? (e1[d] * d2v - e2[d] * d1v) / det : (e2[d] * d1u - e1[d] * d2u) / det;
            const double length = std::sqrt(tangent[0]*tangent[0] + tangent[1]*tangent[1] + tangent[2]*tangent[2]);
            if (length > 1e-12) vertical[axis] += area * std::abs(tangent[2]) / length;
        }
    }
    return vertical[1] >= vertical[0] ? 1u : 0u;
}
// Appends each vertex's (component's) distance to the hair mesh's open border
// to out (FurProfile::hair_border), in node units at the unit's shape.
// Vertices weld by position, so texture seams are not borders. A border edge
// counts only where strands cross it: an edge of one triangle running more
// than 45 degrees off that triangle's strand direction (the painted texture
// axis, fur_hair_axis), on the vertex's own connected piece (lock). Hair
// meshes are mostly separate card strips whose long sides are borders along
// the strands; counting those clamped nearly every strand to nothing (fur_debug
// 3 run, 97523671). Rims, hairlines and lock ends cross the strands.
// Returns the counted border edge count.
std::size_t fur_hair_border(const FigureCpuAsset& figure, const std::array<float,3>& shape, std::uint32_t axis,
                            std::vector<float>& out) {
    const std::size_t count = figure.components.size();
    std::vector<std::array<float,3>> p(count);
    std::vector<std::uint32_t> weld(count, UINT32_MAX);
    std::map<std::array<std::uint32_t,3>, std::uint32_t> ids;
    for (std::size_t c = 0; c < count; ++c) {
        float v[3];
        if (!fur_position(figure, shape, figure.components[c].position, v)) continue;
        p[c] = {v[0], v[1], v[2]};
        std::array<std::uint32_t,3> key{}; std::memcpy(key.data(), v, sizeof(v));
        weld[c] = ids.emplace(key, static_cast<std::uint32_t>(ids.size())).first->second;
    }
    // Connected pieces over welded vertices (union-find).
    std::vector<std::uint32_t> piece(ids.size());
    for (std::uint32_t k = 0; k < piece.size(); ++k) piece[k] = k;
    const auto root = [&](std::uint32_t k) { while (piece[k] != k) k = piece[k] = piece[piece[k]]; return k; };
    struct Edge { unsigned uses = 0; std::uint32_t a = 0, b = 0; bool crosses = false; };
    std::map<std::pair<std::uint32_t,std::uint32_t>, Edge> edges;
    for (std::size_t t = 0; t + 2 < figure.indices.size(); t += 3) {
        const std::uint32_t c[3]{figure.indices[t], figure.indices[t + 1], figure.indices[t + 2]};
        if (c[0] >= count || c[1] >= count || c[2] >= count) continue;
        const std::uint32_t w[3]{weld[c[0]], weld[c[1]], weld[c[2]]};
        if (w[0] == UINT32_MAX || w[1] == UINT32_MAX || w[2] == UINT32_MAX || w[0] == w[1] || w[1] == w[2] || w[2] == w[0]) continue;
        piece[root(w[1])] = root(w[0]); piece[root(w[2])] = root(w[0]);
        // The triangle's strand direction (furHairTangent's rule).
        float tangent[3]{};
        bool has_tangent = false;
        {
            const auto& tc = figure.texture_coordinates;
            const auto texture = [&](unsigned k) { return figure.components[c[k]].texture; };
            if (texture(0) < tc.size() && texture(1) < tc.size() && texture(2) < tc.size()) {
                const float d1u = tc[texture(1)].u - tc[texture(0)].u, d1v = tc[texture(1)].v - tc[texture(0)].v;
                const float d2u = tc[texture(2)].u - tc[texture(0)].u, d2v = tc[texture(2)].v - tc[texture(0)].v;
                const float det = d1u * d2v - d2u * d1v;
                if (std::abs(det) > 1e-12f) {
                    float length = 0;
                    for (unsigned a = 0; a < 3; ++a) {
                        const float e1 = p[c[1]][a] - p[c[0]][a], e2 = p[c[2]][a] - p[c[0]][a];
                        tangent[a] = axis == 0 ? (e1 * d2v - e2 * d1v) / det : (e2 * d1u - e1 * d2u) / det;
                        length += tangent[a] * tangent[a];
                    }
                    has_tangent = length > 1e-20f;
                    if (has_tangent) for (float& value : tangent) value /= std::sqrt(length);
                }
            }
        }
        for (unsigned k = 0; k < 3; ++k) {
            const unsigned j = (k + 1) % 3;
            auto& edge = edges[{(std::min)(w[k], w[j]), (std::max)(w[k], w[j])}];
            ++edge.uses; edge.a = c[k]; edge.b = c[j];
            float e[3], length = 0, along = 0;
            for (unsigned a = 0; a < 3; ++a) { e[a] = p[c[j]][a] - p[c[k]][a]; length += e[a] * e[a]; along += e[a] * tangent[a]; }
            edge.crosses = has_tangent && length > 0 && std::abs(along) < .7071f * std::sqrt(length);
        }
    }
    // Counted border edges per piece.
    std::map<std::uint32_t, std::vector<std::pair<std::array<float,3>, std::array<float,3>>>> border;
    std::size_t counted = 0;
    for (const auto& edge : edges)
        if (edge.second.uses == 1 && edge.second.crosses) {
            border[root(edge.first.first)].push_back({p[edge.second.a], p[edge.second.b]});
            ++counted;
        }
    for (std::size_t c = 0; c < count; ++c) {
        float best = 1e9f;
        const auto own = weld[c] != UINT32_MAX ? border.find(root(weld[c])) : border.end();
        if (own != border.end())
            for (const auto& segment : own->second) {
                float d[3], e[3], dd = 0, de = 0;
                for (unsigned a = 0; a < 3; ++a) {
                    d[a] = p[c][a] - segment.first[a]; e[a] = segment.second[a] - segment.first[a];
                    dd += d[a] * e[a]; de += e[a] * e[a];
                }
                const float f = de > 0 ? std::clamp(dd / de, 0.f, 1.f) : 0.f;
                float r2 = 0;
                for (unsigned a = 0; a < 3; ++a) { const float q = d[a] - e[a] * f; r2 += q * q; }
                best = (std::min)(best, std::sqrt(r2));
            }
        out.push_back(best);
    }
    return counted;
}
}  // namespace

// Fur prototype (fur.h): the unit's fur profile, built once per assembly
// while fur_mode is on. Null for units with neither a furry model nor hair.
std::shared_ptr<const FurProfile> retained_visual_fur_profile(const RetainedVisualSample& sample) {
    if (g_fur_mode <= 0 || !sample.values.unit || !sample.assembly || !sample.prepared) return nullptr;
    auto& prepared = *sample.prepared;
    if (prepared.fur_assembly == sample.assembly) return prepared.fur_profile;
    prepared.fur_assembly = sample.assembly; prepared.fur_profile.reset();
    const auto& assembly = *sample.assembly;
    if (!assembly.skeleton) return nullptr;
    auto profile = std::make_shared<FurProfile>();
    profile->model = assembly.skeleton->visual_reference;
    float coat = 0;
    bool head_fur = true;
    for (const auto& model : kFurModels)
        if (profile->model.find(model.model) != std::string::npos) { coat = model.length; head_fur = model.head; break; }
    const float scale = (std::max)({std::abs(assembly.scale[0]), std::abs(assembly.scale[1]), std::abs(assembly.scale[2])});
    // Creature size: the largest part's vertex sphere (the body) at the unit's
    // own shape, scaled.
    // VisualAssemblyAsset::envelope_radius is not filled for units (0 in the
    // menu run), and the sample's envelope includes animation reach.
    float part_radius = 0;
    for (const auto& part : assembly.parts) {
        std::array<float,4> sphere{};
        if (part.geometry && !part.weapon && !part.arrow && fur_figure_sphere(*part.geometry, assembly.shape, sphere))
            part_radius = (std::max)(part_radius, sphere[3]);
    }
    const float radius = part_radius * scale;
    // The paw/hoof is each limb chain's highest segment present in this assembly.
    int terminal[4]{};
    for (const auto& part : assembly.parts) {
        int segment = 0;
        const int chain = fur_limb_chain(fur_part_stem(part.name), segment);
        if (chain >= 0) terminal[chain] = (std::max)(terminal[chain], segment);
    }
    int tail_segment = 10;
    bool has_hair = false;
    std::vector<std::uint8_t> hair_parts;   // hair parts per node (a shared node gets no border list)
    std::size_t border_edges = 0;
    for (const auto& part : assembly.parts) {
        // A hidden part (no triangles, or a helmet piece a bare portrait head
        // hides) grows nothing and must not set the head sphere.
        if (!part.geometry || part.explicitly_hidden || part.skeleton_node == UINT_MAX || part.weapon || part.arrow) continue;
        if (part.skeleton_node >= 4096) continue;
        if (profile->nodes.size() <= part.skeleton_node) profile->nodes.resize(part.skeleton_node + 1);
        const std::string stem = fur_part_stem(part.name);
        auto& node = profile->nodes[part.skeleton_node];
        if (stem == "hd") {
            profile->head_node = part.skeleton_node;
            profile->head_sphere_valid = fur_figure_sphere(*part.geometry, assembly.shape, profile->head_sphere);
        }
        if (fur_hair_stem(stem)) {
            node.role = float(fur_role_hair);
            if (hair_parts.size() <= part.skeleton_node) hair_parts.resize(part.skeleton_node + 1);
            if (!has_hair) profile->hair_axis = fur_hair_axis(*part.geometry);
            if (hair_parts[part.skeleton_node]++ == 0) {
                node.border = float(profile->hair_border.size());
                border_edges += fur_hair_border(*part.geometry, assembly.shape, profile->hair_axis, profile->hair_border);
            } else node.border = -1;   // two hair parts on one node: its records' vertices are not one list
            has_hair = true;
            continue;
        }
        if (coat <= 0) {
#if EID3D11_FUR_ARMOUR
            // Not a furry creature: every other part may wear fur armour. The
            // fur pass grows strands only on texels of a strand material class
            // (the S1 class texture), so a part without fur grows nothing.
            if (node.role == float(fur_role_none)) {
                node.role = float(fur_role_armour);
                node.length = kFurArmourCoat * radius;
                profile->armour = profile->armour || node.length > 0;
            }
#endif
            continue;
        }
        int segment = 0;
        const int chain = fur_limb_chain(stem, segment);
        FurRole role = chain >= 0 ? (segment < terminal[chain] ? fur_role_limb : fur_role_none) : fur_stem_role(stem);
        if (role == fur_role_head && !head_fur) role = fur_role_none;
        if (role == fur_role_tail && stem[2] - '0' < tail_segment) { tail_segment = stem[2] - '0'; profile->tail_node = part.skeleton_node; }
        if (stem == "hp" && tail_segment == 10) profile->tail_node = part.skeleton_node;
        node.role = float(role);
        node.length = coat * radius * fur_role_factor(role) * (stem == "nc" ? 1.4f : 1.f);   // neck ruff
        profile->fur = profile->fur || node.length > 0;
    }
    if (has_hair) {
        // Shell fuzz on the hair mesh: a fraction of the head size.
        const float hair = profile->head_sphere_valid ? profile->head_sphere[3] * .12f : radius * .02f;
        for (auto& node : profile->nodes)
            if (node.role == float(fur_role_hair)) node.length = hair;
        profile->hair = true;
    }
    if (coat <= 0 && !has_hair && !profile->armour) return nullptr;
    unsigned fur_nodes = 0, hair_nodes = 0;
    for (const auto& node : profile->nodes) {
        fur_nodes += node.length > 0 && node.role != float(fur_role_hair);
        hair_nodes += node.role == float(fur_role_hair);
    }
    unsigned shared_hair_nodes = 0;
    for (const auto parts : hair_parts) shared_hair_nodes += parts > 1;
    char text[384];
    std::snprintf(text, sizeof(text), "model=%s shape=%.2f,%.2f,%.2f scale=%.2f fur_nodes=%u hair_nodes=%u head=%d tail=%d radius=%.3f coat=%.3f head_r=%.3f hair_axis=%c armour=%d hair_border_edges=%u shared_hair_nodes=%u",
        profile->model.c_str(), assembly.shape[0], assembly.shape[1], assembly.shape[2], scale, fur_nodes, hair_nodes,
        int(profile->head_node), int(profile->tail_node), radius, coat, profile->head_sphere[3],
        profile->hair_axis ? 'v' : 'u', int(profile->armour), unsigned(border_edges), shared_hair_nodes);
    bridge_append_probe_event("fur_profile", text);
    // Logged for every unit whose model is listed or has hair, kept only when it grows something.
    if (!profile->fur && !profile->hair && !profile->armour) return nullptr;
    profile->radius = radius;
    prepared.fur_profile = std::move(profile);
    return prepared.fur_profile;
}

namespace {
// The record's profile when any of its nodes grows fur or hair, else null.
// Fur armour grows only in the strand modes: in mode 1 an armour node alone
// attaches nothing (the fur pass would run its vertex stage for nothing).
std::shared_ptr<const FurProfile> fur_record_profile(const RetainedVisualSample& sample,
    const std::uint32_t* parts, std::size_t count) {
    auto profile = retained_visual_fur_profile(sample);
    if (!profile) return nullptr;
    const bool armour = fur_hair_strands(g_fur_mode);
    for (std::size_t k = 0; k < count; ++k) {
        const auto node = sample.assembly->parts[parts[k]].skeleton_node;
        if (node < profile->nodes.size() && profile->nodes[node].length > 0 &&
            (armour || profile->nodes[node].role != float(fur_role_armour))) return profile;
    }
    return nullptr;
}
}  // namespace

bool retained_visual_part_submission(const RetainedVisualSample& sample,std::size_t index,
    const NativeWorldBounds& bounds,NativeContribution contribution,
    double time,std::uint8_t fog,NativePreparedPartSubmission*& output) {
    output=nullptr;
    if (!sample.assembly||!sample.prepared||index>=sample.prepared->parts.size()||
        !contribution.known||(!contribution.color&&!contribution.light_mask)||!finite_world_bounds(bounds)) return false;
    auto& prepared=*sample.prepared;const auto& assembly=*sample.assembly;const auto& part=assembly.parts[index];
    if (!part.source_available||!part.geometry||!part.material||!prepared.gpu_pose.binding||
        part.skeleton_node>=prepared.gpu_pose.binding->tracks.size()) return false;
    auto& target=prepared.parts[index];auto& view=target.record;
    if (target.bound_assembly!=sample.assembly) {
        view={};const auto& figure=*part.geometry;
        view.world.enabled=true;view.world.identity=&figure.identity;view.world.shape_count=figure.shape_count;
        if(!sample.values.unit&&!assembly.sways&&!astral_kind(assembly,part))view.lod_figure=&figure;
        view.geometry_revision={1,0,0,0,0};view.geometry_revision_valid=true;
        view.positions=figure.positions.data();view.position_groups=figure.position_groups;
        view.normals=figure.normals.data();view.normal_groups=figure.normal_groups;
        view.texture_coordinates=figure.texture_coordinates.data();view.texture_coordinate_count=static_cast<UINT>(figure.texture_coordinates.size());
        view.components=figure.components.data();view.component_count=static_cast<UINT>(figure.components.size());
        view.indices=figure.indices.data();view.index_count=static_cast<UINT>(figure.indices.size());
        view.bounds_valid=true;view.bounds_flags=2;view.semantic_kind=SemanticKind::figure;
        retained_visual_part_fixed_state(sample,view.owned_fixed_state);
        // Astral rift veils and clumps: blended over everything without
        // writing depth, nearly every texel kept (astral_rift.h).
        if (astral_kind(assembly,part)) { view.owned_fixed_state.z_write_enable=FALSE; view.owned_fixed_state.alpha_ref=1; }
        if (part.surface) {
            view.owned_texture.identity=part.surface->id;view.owned_texture.generation=part.surface->generation;
            view.owned_texture.surface=part.surface.get();
        } else if (part.texture&&part.texture->source&&!part.texture->mips.empty()&&part.texture->mips.size()<=target.texture_mips.size()) {
            const auto& texture=*part.texture;
            for (std::size_t mip=0;mip<texture.mips.size();++mip) {
                const auto& source=texture.mips[mip];
                if (source.byte_offset>texture.source->size()||source.bytes>texture.source->size()-source.byte_offset) return false;
                target.texture_mips[mip]={texture.source->data()+source.byte_offset,source.width,source.height,source.bytes};
            }
            view.owned_texture.value_source=static_cast<UINT>(texture.value_source)+1;
            view.owned_texture.identity=texture.value_source==TextureValueSource::archive?1:texture.value_serial;
            view.owned_texture.generation=1;view.owned_texture.resource=texture.value_source==TextureValueSource::archive?&texture.identity:nullptr;
            view.owned_texture.format=texture_format(texture.format);view.owned_texture.mip_count=static_cast<UINT>(texture.mips.size());
            view.owned_texture.opaque_mips=texture.opaque_mips;
        } else return false;
        if (!part.material_name.empty()) view.owned_texture.material_name=&part.material_name;
        if(part.environment_material) {
            const auto& m=*part.environment_material;
            view.skn_material=m.native();
            if(m.alpha==0)view.owned_texture.opaque_mips=UINT_MAX;
            view.owned_fixed_state.z_write_enable=m.alpha!=2;
            view.owned_fixed_state.alpha_func=D3DCMP_GREATER;
            view.owned_fixed_state.cull_mode=m.two_sided?D3DCULL_NONE:D3DCULL_CW;
            view.owned_fixed_state.alpha_test_enable=m.alpha==1;
            view.owned_fixed_state.alpha_ref=static_cast<UINT>(m.cutoff*255);
            view.owned_fixed_state.alpha_blend_enable=m.alpha==2;
            view.owned_fixed_state.address_u=m.address_u;view.owned_fixed_state.address_v=m.address_v;
        }
        target.bound_assembly=sample.assembly;
        if (++target.record_revision==0) ++target.record_revision;
        view.world.record_revision=target.record_revision;view.world.owner=&prepared.gpu_pose.owner;
        view.prepared_identity={sample.lifecycle.id,sample.lifecycle.generation,sample.lifecycle.positive_world_epoch,
            sample.resource.serial,static_cast<UINT>(index),0,1,true};
        view.world.pose_node=part.skeleton_node;view.world.geometry_radius=target.geometry_radius;
    }
    const bool static_lod=view.lod_figure && prepared.gpu_pose.binding->neutral &&
        part.texture_type!=13 && part.texture_type!=14 && part.texture_type!=16;
    if(view.world.static_lod!=static_lod){
        view.world.static_lod=static_lod;
        if(++target.record_revision==0)++target.record_revision;
        view.world.record_revision=target.record_revision;
    }
    if (view.world.geometry_radius != target.geometry_radius) {
        view.world.geometry_radius=target.geometry_radius;
        if (++target.record_revision==0) ++target.record_revision;
        view.world.record_revision=target.record_revision;
    }
    view.owned_texture.mips=target.texture_mips.data();
    view.prepared_mesh=&target.mesh;view.prepared_material=&target.material;view.prepared_wound_material=&target.wound_material;
    NativePreparedPartValues values;
    if (!retained_visual_part_values(sample,part,time,fog,values)) return false;
    auto& submission=target.submission;
    if(!submission.values_revision||std::memcmp(&submission.values,&values,sizeof(values))!=0){
        submission.values=values;if(++submission.values_revision==0)++submission.values_revision;
    }
    {const auto part_index=static_cast<std::uint32_t>(index);
     submission.fur=fur_record_profile(sample,&part_index,1);submission.fur_node_base=part.skeleton_node;}
    submission.source=&view;submission.bounds=bounds;submission.contribution=contribution;
    output=&submission;return true;
}

namespace {
// Parts merge when every value their records derive from the part is equal:
// texture surface, material values, flags, texture type and shape layout.
// Weapons and arrows toggle visibility in the field and time-scrolled
// texture types (13, 14, 16) shift UVs per part, so they stay per part.
bool merge_part_eligible(const VisualPartAsset& part) {
    // Colour/depth submit the whole group, so service FIGs must not enter it.
    if (unit_service_node(part.name.c_str())) return false;
    // Fur prototype, strands: a unit's hair part stays its own record (its
    // strands and undercoat; retained_visual_part_values).
    if (fur_hair_strands(g_fur_mode) && fur_hair_stem(fur_part_stem(part.name))) return false;
    // A merged draw contains every member's indices. Apply the unit marker
    // policy before constructing it: colour and shadow admission is per group.
    return part.source_available && part.geometry && part.material && part.surface && !part.weapon && !part.arrow &&
        !part.explicitly_hidden && !unit_service_node(part.name.c_str()) &&
        part.texture_type!=13 && part.texture_type!=14 && part.texture_type!=16 &&
        part.geometry->position_groups && part.geometry->shape_count &&
        part.geometry->positions.size()==std::size_t(part.geometry->shape_count)*part.geometry->position_groups;
}
bool merge_materials_equal(const MaterialCpuAsset& a,const MaterialCpuAsset& b) noexcept {
    return &a==&b || (a.diffuse==b.diffuse && a.ambient==b.ambient && a.specular==b.specular && a.emissive==b.emissive &&
        a.previous_highlight_emissive==b.previous_highlight_emissive &&
        a.has_previous_highlight_emissive==b.has_previous_highlight_emissive &&
        a.oldest_highlight_emissive==b.oldest_highlight_emissive &&
        a.has_oldest_highlight_emissive==b.has_oldest_highlight_emissive && a.power==b.power);
}
bool merge_parts_compatible(const VisualPartAsset& a,const VisualPartAsset& b) noexcept {
    return a.surface->id==b.surface->id && a.surface->generation==b.surface->generation &&
        merge_materials_equal(*a.material,*b.material) && a.texture_type==b.texture_type &&
        a.material_type==b.material_type && a.figure_flags==b.figure_flags && a.material_flags==b.material_flags &&
        a.texture_flags==b.texture_flags && a.diffuse==b.diffuse && a.specular==b.specular &&
        a.casts_shadow==b.casts_shadow && a.geometry->shape_count==b.geometry->shape_count;
}
} // namespace

namespace {
// One immutable merged geometry per content identity, shared by every
// character whose merged parts are identical; the last holder frees it.
template<class Build>
std::shared_ptr<const VisualPreparedResources::MergedGeometry> merged_geometry(const std::string& content, Build build) {
    static std::mutex lock;
    static std::map<std::string,std::weak_ptr<const VisualPreparedResources::MergedGeometry>> cache;
    std::lock_guard<std::mutex> guard(lock);
    if (const auto found=cache.find(content); found!=cache.end())
        if (auto shared=found->second.lock()) return shared;
    if (cache.size()>256) for (auto entry=cache.begin();entry!=cache.end();)
        entry=entry->second.expired()?cache.erase(entry):std::next(entry);
    auto geometry=std::make_shared<VisualPreparedResources::MergedGeometry>();
    build(*geometry);
    geometry->identity.canonical_name=content;
    std::shared_ptr<const VisualPreparedResources::MergedGeometry> result=std::move(geometry);
    cache[content]=result;
    return result;
}
}

#include "skn_submission.inl"

bool retained_visual_merged_groups(const RetainedVisualSample& sample) {
    if (!sample.assembly || !sample.prepared || !sample.values.unit) return false;
    auto& prepared=*sample.prepared;const auto& assembly=*sample.assembly;
    const bool fur_hair=fur_hair_strands(g_fur_mode);
    if (prepared.merged_assembly==sample.assembly && prepared.merged_fur_hair==fur_hair &&
        prepared.merged_physique==sample.physique) return true;
    prepared.merged_physique=sample.physique;
    prepared.merged_fur_hair=fur_hair;prepared.merged_assembly=nullptr;
    if (!prepared.gpu_pose.binding) return false;
    prepared.merged_groups.clear();prepared.merged_part_group.assign(assembly.parts.size(),-1);
    // Without a pose binding yet the parts draw singly for now; do not record
    // the assembly as merged, or it never merges once the binding exists.
    if (!prepared.gpu_pose.binding) return true;
    bool skn_present=false;
    try {
     const auto skn_asset=skn::select_visual(assembly.skeleton->visual_reference,assembly.main_hero_visual,skn_present);
     prepared.skn_selected=skn_present;
     if(skn_present){
      const bool built=build_skn_groups(sample,skn_asset);
      if(built && skn_asset->gltf_asset) {
       const auto& body=*skn_asset->gltf_asset;
       for(const auto& item:prepare_gltf_equipment(sample,body).items) {
        std::vector<unsigned> nodes;
        if(item.asset->wearable) {
         nodes=item.body_nodes;
         for(auto& n:nodes)n+=static_cast<unsigned>(assembly.skeleton->nodes.size());
        } else {
         const auto found=std::find_if(body.geometry.joints.begin(),body.geometry.joints.end(),
             [&](const skn::Joint& j){return j.name==item.socket;});
         if(item.asset->mechanism.empty() && !item.asset->projectile_aim) {
          nodes.push_back(static_cast<unsigned>(assembly.skeleton->nodes.size()+
              (found-body.geometry.joints.begin())));
         } else {
          const auto start=unsigned(assembly.skeleton->nodes.size()+body.geometry.joints.size())+
              body.gpu_animation->morphs+item.mechanism_offset;
          const auto count=item.asset->projectile_aim ? 1u : unsigned(item.asset->mechanism.size());
          for(unsigned j=0;j<count;++j)nodes.push_back(start+j);
         }
        }
        build_skn_groups(sample,skn::from_gltf(item.asset),&nodes,item.weapon,item.arrow,
            &item.base_color_overrides,&item.metallic_roughness_overrides,&item.material_identities,item.physique_anchors.get(),
            item.asset->mechanism.empty() && !item.asset->projectile_aim && item.offset!=std::array<float,3>{} ? &item.offset : nullptr);
       }
      }
      if(built)prepared.merged_assembly=sample.assembly;
      return built;
     }
    } catch(const std::exception& error) {
     // Reject the selected visual atomically. An invalid SKN cannot expose
     // a partly built group or silently fall back to the legacy FIG visual.
     prepared.skn_selected=true;prepared.merged_groups.clear();
     prepared.merged_part_group.assign(assembly.parts.size(),0);
     prepared.merged_assembly=sample.assembly;
     skn::report_rejection(sample.assembly->skeleton->visual_reference,error.what());
     return true;
    }
    prepared.merged_assembly=sample.assembly;
    std::vector<std::vector<std::uint32_t>> members;
    for (std::uint32_t index=0;index<assembly.parts.size();++index) {
        const auto& part=assembly.parts[index];
        if (!merge_part_eligible(part) || astral_kind(assembly,part) ||
            part.skeleton_node>=prepared.gpu_pose.binding->tracks.size()) continue;
        std::size_t group=0;
        while (group<members.size() && !merge_parts_compatible(assembly.parts[members[group][0]],part)) ++group;
        if (group==members.size()) members.emplace_back();
        members[group].push_back(index);
    }
    std::size_t groups=0;for (const auto& list:members) groups+=list.size()>1;
    prepared.merged_groups.resize(groups);
    std::size_t next=0;
    for (const auto& list:members) {
        if (list.size()<2) continue;
        auto& group=prepared.merged_groups[next];
        const auto shapes=assembly.parts[list[0]].geometry->shape_count;
        std::uint64_t position_groups=0,normal_groups=0,uvs=0,components=0,indices=0;
        for (const auto index:list) {
            const auto& figure=*assembly.parts[index].geometry;
            position_groups+=figure.position_groups;normal_groups+=figure.normal_groups;
            uvs+=figure.texture_coordinates.size();components+=figure.components.size();indices+=figure.indices.size();
        }
        // Component, position and index values are 16-bit in the mesh format.
        if (position_groups*4>0xffffu || normal_groups*4>0xffffu || uvs>0xffffu || components>0xffffu) {
            prepared.merged_groups.pop_back();continue;   // the rest stay per part
        }
        group.parts=list;group.shape_count=shapes;
        // Preserve X64P01 bounds before narrowing to this x86 container size.
        const VisualPreparedResources::MergedGeometry limits;
        if (indices > limits.indices.max_size() ||
            (position_groups && shapes > limits.positions.max_size() / position_groups)) {
            prepared.merged_groups.pop_back(); continue;
        }
        group.position_groups=static_cast<std::uint32_t>(position_groups);
        group.normal_groups=static_cast<std::uint32_t>(normal_groups);
        std::string content="merged-parts";
        for (const auto index:list) {
            const auto& part=assembly.parts[index];
            content+='|'+part.geometry->identity.key()+'@'+std::to_string(part.skeleton_node);
            prepared.merged_part_group[index]=static_cast<std::int32_t>(next);
        }
        group.geometry=merged_geometry(content,[&](VisualPreparedResources::MergedGeometry& geometry) {
            geometry.positions.resize(std::size_t(shapes)*position_groups);geometry.normals.reserve(normal_groups);
            geometry.texture_coordinates.reserve(uvs);geometry.components.reserve(components);
            geometry.component_parts.reserve(components);geometry.indices.reserve(indices);
            std::uint32_t position_base=0,normal_base=0,uv_base=0,component_base=0;
            for (const auto index:list) {
                const auto& part=assembly.parts[index];const auto& figure=*part.geometry;
                for (std::uint32_t shape=0;shape<shapes;++shape)
                    std::copy_n(figure.positions.begin()+std::ptrdiff_t(shape)*figure.position_groups,figure.position_groups,
                        geometry.positions.begin()+std::ptrdiff_t(shape)*position_groups+position_base/4);
                geometry.normals.insert(geometry.normals.end(),figure.normals.begin(),figure.normals.end());
                geometry.texture_coordinates.insert(geometry.texture_coordinates.end(),
                    figure.texture_coordinates.begin(),figure.texture_coordinates.end());
                for (const auto& component:figure.components) {
                    geometry.components.push_back({static_cast<std::uint16_t>(component.position+position_base),
                        static_cast<std::uint16_t>(component.normal+normal_base),
                        static_cast<std::uint16_t>(component.texture+uv_base)});
                    geometry.component_parts.push_back(part.skeleton_node|(position_base<<16));
                }
                for (const auto value:figure.indices) geometry.indices.push_back(static_cast<std::uint16_t>(value+component_base));
                position_base+=figure.position_groups*4;normal_base+=figure.normal_groups*4;
                uv_base+=static_cast<std::uint32_t>(figure.texture_coordinates.size());
                component_base+=static_cast<std::uint32_t>(figure.components.size());
            }
        });
        ++next;
    }
    prepared.merged_groups.resize(next);
    return true;
}

bool retained_visual_group_submission(const RetainedVisualSample& sample,std::size_t group_index,
    const NativeWorldBounds& bounds,NativeContribution contribution,double time,std::uint8_t fog,
    NativePreparedPartSubmission*& output) {
    output=nullptr;
    if (!sample.assembly||!sample.prepared||group_index>=sample.prepared->merged_groups.size()||
        !contribution.known||(!contribution.color&&!contribution.light_mask)||!finite_world_bounds(bounds)||
        !sample.prepared->gpu_pose.binding) return false;
    auto& prepared=*sample.prepared;auto& group=prepared.merged_groups[group_index];
    if((group.gltf_weapon && sample.visibility.hide_weapons) ||
       (group.gltf_arrow && sample.visibility.hide_arrow)) return false;
    const auto& part=sample.assembly->parts[group.parts[0]];
    auto& view=group.record;
    // Every node lies within the already-scaled world selection envelope.
    // Twice that world radius around node zero bounds every merged part;
    // nativeCurrentBounds must not apply the node's scale to it again.
    const auto& root_matrix=prepared.gpu_pose.root_to_world;
    double squared_radius=0;
    for(unsigned axis=0;axis<3;++axis){
      const double d=(std::max)(std::abs(double(bounds.minimum[axis])-root_matrix[axis*4+3]),
                               std::abs(double(bounds.maximum[axis])-root_matrix[axis*4+3]));
      squared_radius+=d*d;
    }
    const float radius=group.skn_asset?2*static_cast<float>(std::sqrt(squared_radius)):
        2*(std::max)({bounds.maximum[0]-bounds.minimum[0],bounds.maximum[1]-bounds.minimum[1],
            bounds.maximum[2]-bounds.minimum[2]})*.5f;
    if (!view.world.enabled) {
        view={};
        const auto& geometry=*group.geometry;
        view.world.enabled=true;view.world.identity=&geometry.identity;view.world.shape_count=group.shape_count;
        view.geometry_revision={1,0,0,0,0};view.geometry_revision_valid=true;
        view.positions=geometry.positions.data();view.position_groups=group.position_groups;
        view.normals=geometry.normals.data();view.normal_groups=group.normal_groups;
        view.texture_coordinates=geometry.texture_coordinates.data();view.texture_coordinate_count=static_cast<UINT>(geometry.texture_coordinates.size());
        view.components=geometry.components.data();view.component_count=static_cast<UINT>(geometry.components.size());
        if(group.skn_asset){view.skin_vertices=geometry.skin_vertices.data();view.lods=&geometry.skn_lods;view.world.static_lod=true;}
        else view.component_parts=geometry.component_parts.data();
        view.indices=geometry.indices.data();view.index_count=static_cast<UINT>(geometry.indices.size());
        view.bounds_valid=true;view.bounds_flags=2;view.semantic_kind=SemanticKind::figure;
        retained_visual_part_fixed_state(sample,view.owned_fixed_state);
        const bool assembled_texture = group.skn_asset && !group.skn_asset->gltf_asset && part.surface &&
            part.assembled_character_texture && group.skn_asset->asset.materials[group.skn_material].maps[0].empty();
        if(!group.skn_asset || assembled_texture){
         view.owned_texture.identity=part.surface->id;view.owned_texture.generation=part.surface->generation;
         view.owned_texture.surface=part.surface.get();
        }else{
         const auto& material=group.skn_asset->asset.materials[group.skn_material];
         const auto& texture=group.skn_base_color_override ? *group.skn_base_color_override :
             group.skn_asset->textures[group.skn_material][0];
         skn::apply_material_view(view,material,group.skn_asset->textures[group.skn_material],
             texture,group.skn_metallic_roughness_override.get(),group.skn_material_identity.get());
        }
        if (assembled_texture) view.owned_texture.material_name=&part.texture_source_name;
        if (!group.skn_asset && !part.material_name.empty()) view.owned_texture.material_name=&part.material_name;
        if (++group.record_revision==0) ++group.record_revision;
        view.world.record_revision=group.record_revision;view.world.owner=&prepared.gpu_pose.owner;
        view.prepared_identity={sample.lifecycle.id,sample.lifecycle.generation,sample.lifecycle.positive_world_epoch,
            sample.resource.serial,static_cast<UINT>(0x10000u+group_index),0,1,true};
        view.world.pose_node=0;
    }
    if (!(view.world.geometry_radius>=radius)) {        // grow-only: stable record while the unit animates
        view.world.geometry_radius=group.geometry_radius=radius*1.25f;
        if (++group.record_revision==0) ++group.record_revision;
        view.world.record_revision=group.record_revision;
    }
    if(!group.skn_asset)view.owned_texture.mips=group.texture_mips.data();
    view.prepared_mesh=&group.mesh;view.prepared_material=&group.material;view.prepared_wound_material=&group.wound_material;
    // The members share every value input, so the first member's values hold
    // for all of them; 0x40 selects the per-component part node on the GPU.
    NativePreparedPartValues values;
    if (!retained_visual_part_values(sample,part,time,fog,values)) return false;
    values.flags|=0x40u;
    if(group.skn_asset && (group.skn_asset->gltf_asset || !part.assembled_character_texture || !group.skn_asset->asset.materials[group.skn_material].maps[0].empty())){
     const auto& m=group.skn_asset->asset.materials[group.skn_material];
     if(m.alpha!=0)values.alpha*=m.base[3];

    }
    auto& submission=group.submission;
    if(!submission.values_revision||std::memcmp(&submission.values,&values,sizeof(values))!=0){
        submission.values=values;if(++submission.values_revision==0)++submission.values_revision;
    }
    submission.fur=group.skn_asset?nullptr:fur_record_profile(sample,group.parts.data(),group.parts.size());submission.fur_node_base=0;
    submission.source=&view;submission.bounds=bounds;submission.contribution=contribution;
    output=&submission;return true;
}

bool retained_visual_group_depth_revision(const RetainedVisualSample& sample,std::size_t group_index,
    double time,NativeShadowCasterRevision& revision) noexcept {
    revision={};
    if(!sample.assembly||!sample.prepared||group_index>=sample.prepared->merged_groups.size())return false;
    auto& group=sample.prepared->merged_groups[group_index];
    const auto& part=sample.assembly->parts[group.parts[0]];const auto& pose=sample.prepared->gpu_pose;
    if(!pose.binding||!part.material)return false;
    VisualPreparedResources::DepthPose value{};
    value.root=pose.root_to_world;value.shape=pose.shape;value.center=pose.center_shift;
    value.frame=pose.frame;value.fraction=pose.fraction;value.height=pose.height_scale;
    value.alpha=retained_visual_effective_alpha(sample,part);
    if(!retained_visual_texture_shift(part.texture_type,time,value.u,value.v))return false;
    // Group sources are fixed for its assembly (a rebuild starts a new group).
    if(!group.depth_revision || !(group.depth_pose==value) || group.depth_binding!=pose.binding){
        group.depth_revision=next_retained_shadow_revision();group.depth_pose=value;group.depth_binding=pose.binding;
    }
    revision={sample.lifecycle.id,static_cast<UINT>(0x10000u+group_index),sample.lifecycle.generation,
        sample.lifecycle.positive_world_epoch,sample.resource.serial,group.depth_revision,false};
    return true;
}

#ifdef EID3D11_TESTING
bool retained_visual_current_nodes(const RetainedVisualSample& sample,
    std::shared_ptr<const std::vector<VisualNodeSample>>& nodes) {
    nodes.reset();
    if (!sample.assembly || !sample.assembly->skeleton || !sample.prepared) return false;
    const auto& assembly = *sample.assembly;
    VisualPreparedResources::NodeKey key;
    key.model=assembly.skeleton->model_container.key();
    key.bones=assembly.skeleton->bone_container.key();
    key.shape=retained_visual_shape(sample); key.center_shift=assembly.center_shift;

    if (!sample.selection.neutral) {
        if (!sample.selection.clip || !sample.selection.database) return false;
        key.animation=sample.selection.clip->identity.key();
        key.database=sample.selection.database->identity.key();
        key.phase=sample.pose.phase;
    }
    auto& prepared=*sample.prepared;
    if (prepared.nodes && prepared.node_key==key) { nodes=prepared.nodes; return true; }
    auto next=std::make_shared<std::vector<VisualNodeSample>>();
    if (!retained_visual_nodes(sample,*next)) return false;
    prepared.node_key=std::move(key);
    prepared.nodes=std::move(next);
    if (++prepared.node_generation==0) ++prepared.node_generation;
    nodes=prepared.nodes;
    return true;
}

#endif

bool retained_visual_preserve_color_order(const RetainedVisualSample& sample,
    const VisualPartAsset& part) noexcept {
    if (sample.assembly && astral_kind(*sample.assembly,part)) return true;
    if (!part.material || retained_visual_effective_alpha(sample,part) != 1.f ||
        ((part.figure_flags&0x100u) && (part.diffuse>>24)!=255u))
        return true;
    if(part.environment_material)return part.environment_material->alpha==2;
    // Actual alpha1 content removes the old format-only order constraint.
    // Other textures retain their established format-based ordering policy.
    if (part.surface) {
        if (part.surface->alpha_fully_opaque) return false;
        const auto mask=part.surface->format.dwRGBAlphaBitMask;
        return mask && (mask & (mask-1));
    }
    if (!part.texture) return false;
    const auto count=part.texture->mips.size();
    if (count && count<=32) {
        const auto selected=count==32?UINT32_MAX:(std::uint32_t{1}<<count)-1;
        if ((part.texture->opaque_mips&selected)==selected) return false;
    }
    return part.texture->format==MmpFormat::dxt3 ||
        part.texture->format==MmpFormat::dxt5 ||
        part.texture->format==MmpFormat::argb4444 ||
        part.texture->format==MmpFormat::argb8888;
}

} // namespace owned
