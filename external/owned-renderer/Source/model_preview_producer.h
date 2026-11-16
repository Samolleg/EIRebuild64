#pragma once
#include "contact_blend.h"
#include "fur.h"
#include "native_instance_flags.h"
#include <d3d.h>
#include "model_draw.h"
#include "figure_service_nodes.h"
#include "model_preview_geometry.h"
#include "model_preview_material.h"
#include "gltf_asset.h"

namespace owned {

// Completed render facts only. Source objects and native memory reads remain
// in their adapters; queued previews retain values, never these callbacks.
struct ModelPreviewPart {
    std::size_t ordinal = 0;
    bool hidden = false; // Current caller membership; a UI view may show a previously hidden part.
    ModelPreviewMaterial material{};
    NativeOwnedFixedState fixed_state{};
    bool fixed_state_supplied = false;
    std::uintptr_t wound = 0;
    std::uint32_t visible = 1;
};
struct ModelPreviewInput {
    const RetainedVisualSample& sample;
    std::array<float,16> object_to_world{}, world_to_clip{};
    double texture_time = 0;
    std::uint8_t fog = 255;
    std::uint64_t serial = 0;
    bool ground_contact = true;
};

// One synchronous object transaction for source, immediate retail and queued
// retail previews. Admission/set transitions, private pose placement, geometry,
// material and wound execution share this loop. Interaction output belongs to
// the caller; this producer never writes FIG_VISIBLE or OBJ_VISIBLE.
template<class Read, class Bind, class Prepare, class Submit, class Wound>
bool produce_model_preview(const ModelPreviewInput& input, std::size_t count,
    Read&& read, Bind&& bind, Prepare&& prepare, Submit&& submit, Wound&& wound,
    ModelPartResult& result) {
    result = {};
    NativeWorldGeometry::Pose* pose = nullptr;
    if (!retained_visual_gpu_pose(input.sample, pose) || !pose) return false;
    pose->root_to_world = input.object_to_world;
    // Registry models own their geometry and materials in every view. Do not
    // enumerate FIG visibility or bind a legacy equipment atlas for this path.
    const auto replacement = input.sample.prepared->motion_asset;
    if (replacement && replacement->gltf_asset) {
        if (!retained_visual_merged_groups(input.sample) ||
            !input.sample.prepared->skn_selected) return false;
        const auto& groups = input.sample.prepared->merged_groups;
        if (groups.empty()) return false;
        NativeWorldBounds bounds{};
        float scaleSquared = 0;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                scaleSquared += input.object_to_world[r*4+c]*input.object_to_world[r*4+c];
        const float radius = (input.sample.prepared->gltf_equipment ? input.sample.prepared->gltf_equipment->radius : replacement->gltf_asset->animation_radius) * std::sqrt(scaleSquared) + 1e-4f;
        for (unsigned c = 0; c < 3; ++c) {
            bounds.minimum[c] = input.object_to_world[c*4+3] - radius;
            bounds.maximum[c] = input.object_to_world[c*4+3] + radius;
        }
        bounds.valid = true;
        // OPAQUE/MASK establish depth first; BLEND uses the native triangle
        // ordering path, with the same material depth/cull state as the world.
        for (unsigned pass = 0; pass < 2; ++pass) for (std::size_t i = 0; i < groups.size(); ++i) {
            const auto& group = groups[i];
            if((group.gltf_weapon && input.sample.visibility.hide_weapons) ||
                (group.gltf_arrow && input.sample.visibility.hide_arrow)) continue;
            const auto& material = group.skn_asset->asset.materials[group.skn_material];
            if ((material.alpha == 2) != (pass != 0)) continue;
            NativePreparedPartSubmission* record = nullptr;
            if (!retained_visual_group_submission(input.sample, i, bounds, {true,true,0},
                    input.texture_time, input.fog, record) || !record || !record->source) return false;
            NativeGeometryView view = *record->source;
            // Pass admission lives on the submission, not its immutable source.
            // The native queue requires an explicit color pass for posed views.
            view.contribution = record->contribution;
            view.world.pose = pose; view.world.owner = &pose->owner;
            view.world.object_to_world = input.object_to_world;
            view.world.world_to_clip = input.world_to_clip; view.object_to_clip = input.world_to_clip;
            view.prepared_identity.presentation_serial = input.serial;
            view.bounds_minimum = bounds.minimum; view.bounds_maximum = bounds.maximum;
            view.flags = record->values.flags & ~(4u | 8u);
            if (!input.ground_contact) view.flags &= ~native_ground_contact_flag;
            view.lighting.enabled = true;
            view.lighting.material_diffuse = {1,1,1};
            view.lighting.material_alpha = material.alpha == 0 ? 1.f : material.base[3];
            view.lighting.object_to_world = input.object_to_world;
            view.ordering = NativeOrdering::derive_from_state;
            view.interface_preview = true;
            view.fog = input.fog;
            ModelPreviewPart part; part.ordinal = group.parts.front();
            if (!submit(view, part)) return false;
            ++result.submitted; result.visible = 1;
        }
        return true;
    }
    return produce_model_parts(count, read, bind,
        [&](std::size_t index, std::uint32_t& visible) {
            ModelPreviewPart part;
            if (!prepare(index, part) || !input.sample.assembly ||
                part.ordinal >= input.sample.assembly->parts.size()) return false;
            const auto& asset = input.sample.assembly->parts[part.ordinal];
            if (part.hidden || (input.sample.values.unit && unit_service_node(asset.name.c_str())))
                return true;
            NativeGeometryView view;
            if (!prepare_model_preview_geometry(input.sample, part.ordinal, *pose,
                    input.object_to_world, input.world_to_clip, input.texture_time,
                    input.fog, input.serial, view)) return false;
            if (!input.ground_contact) view.flags &= ~native_ground_contact_flag;
            view.fog = input.fog;
            if (part.fixed_state_supplied) view.owned_fixed_state = part.fixed_state;
            const auto unit_flag = view.flags & native_unit_flag;
            apply_model_preview_material(view, part.material);
            view.flags |= unit_flag;
            if (fur_strand_hair_record(view.fur ? view.fur->get() : nullptr, view.fur_node_base, 0, g_fur_mode)) {
                view.lighting.material_alpha = 0;
                view.owned_fixed_state.alpha_test_enable = TRUE;
                view.owned_fixed_state.alpha_func = D3DCMP_GREATER;
                view.owned_fixed_state.alpha_ref = 0;
                view.owned_fixed_state.z_write_enable = FALSE;
            }
            if (!submit(view, part)) return false;
            if (part.wound) {
                if (!wound(index, part)) return false;
                view.wound_surface_interface = part.wound;
                view.wound_only = true;
                if (!submit(view, part)) return false;
            }
            visible = part.visible;
            return true;
        }, result);
}
} // namespace owned
