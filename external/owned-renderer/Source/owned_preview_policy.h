#pragma once

#include "retained_static_scene.h"
#include "model_draw.h"
#include "figure_service_nodes.h"

namespace owned {

// The HUD panel and the inventory draw a world object a second time through
// the same object draw (CObject3DClientSpecific::Draw, routes
// panel_preview/inventory_preview). A preview that fails or is omitted must
// not write that object's world pick state: OBJ_VISIBLE and the screen
// rectangles that PickFirstMapObject/IsCanSelect read belong to its world
// draw. A cleared OBJ_VISIBLE from a failed preview made a hovered boar
// unpickable whenever its panel figure was shown.
inline bool uncommitted_draw_clears_world_pick_state(bool preview_route) noexcept {
    return model_draw_writes_pick_state(preview_route ? ModelDrawDomain::preview : ModelDrawDomain::normal);
}

// A FIG without triangles (Dejavu unanwibo.mod chelust.001/.002) is an
// explicitly hidden part. It adds no pixels, and the native mesh path rejects
// a view with no indices, so the preview must skip it as the world path does.
inline bool preview_submits_part(const VisualPartAsset& part, bool unit = false) noexcept {
    return !part.explicitly_hidden && !(unit && unit_service_node(part.name.c_str()));
}

} // namespace owned
