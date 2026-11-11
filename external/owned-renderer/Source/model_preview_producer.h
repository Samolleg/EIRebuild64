#pragma once
#include "contact_blend.h"
#include "fur.h"
#include "native_instance_flags.h"
#include <d3d.h>
#include "model_draw.h"
#include "figure_service_nodes.h"
#include "model_preview_geometry.h"
#include "model_preview_material.h"

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
