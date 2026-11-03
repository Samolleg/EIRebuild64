#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace owned {
struct NeighbourPlacement {
    std::string profile, allod, zone, mpr, component, mpr_path, mob_path;
    double x = 0, y = 0, width = 0, height = 0;
};
struct NeighbourSelection {
    NeighbourPlacement placement;
    double x = 0, y = 0;
};
// Saved-layout coordinates are authoritative, including author-labelled inferred
// values. Contacts are closed rectangles; no epsilon, inferred route or gap repair.
std::vector<NeighbourPlacement> parse_neighbour_layout(const std::string& json);
// The layout entry of the active map (throws when absent or ambiguous).
const NeighbourPlacement& neighbour_layout_active(
    const std::vector<NeighbourPlacement>& layout, const std::string& profile,
    const std::string& mpr, const std::string& allod = {}, const std::string& zone = {});
std::vector<NeighbourSelection> select_neighbour_layout(
    const std::vector<NeighbourPlacement>& layout, const std::string& profile,
    const std::string& mpr, const std::string& allod = {}, const std::string& zone = {});
std::string neighbour_key(std::string value);
// Axis-aligned map rectangle in active-map coordinates (active is 0,0..w,h).
struct NeighbourBounds { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
// Raw border-fog edges of one rendered map piece. A side that adjacent
// rendered pieces continue along its whole length moves out to the nearest of
// their far edges; every other side keeps the piece edge, so the band still
// hides the void there. Closed contact, no epsilon, as in the selection.
NeighbourBounds neighbour_fog_edges(const NeighbourBounds& piece,
    const std::vector<NeighbourBounds>& rendered);
// Where rendered pieces touch each side of a piece, partly or wholly. Sides in
// seam order: 0 x0 (west), 1 y0 (south), 2 x1 (east), 3 y1 (north); lo..hi is
// the touched run along the side's line (y for 0/2, x for 1/3) that shares
// most of the side, empty when lo >= hi. The run includes contacts between
// other rendered pieces on the same line, so it may extend past the side's
// ends where neighbours continue beside each other. Closed contact, no
// epsilon, as in neighbour_fog_edges.
struct NeighbourContacts {
    std::array<double, 4> lo{}, hi{};
    bool any(int side) const noexcept { return lo[side] < hi[side]; }
};
NeighbourContacts neighbour_fog_contacts(const NeighbourBounds& piece,
    const std::vector<NeighbourBounds>& rendered);
// Base-terrain normals along one side of a rendered map piece, one per
// authored edge vertex (1 m apart), in active-map coordinates. side: 0 = x0,
// 1 = x1, 2 = y0, 3 = y1; line is that side's coordinate and start the
// coordinate of the first vertex along it.
struct NeighbourEdgeStrip {
    unsigned side = 0;
    double line = 0, start = 0;
    std::vector<std::array<float, 3>> normals;
    // Base-terrain vertex position (authored XY jitter included) at each
    // edge vertex, in active-map coordinates.
    std::vector<std::array<float, 3>> positions;
    // The terrain shape values at each edge vertex (owned_terrain_slope_normal
    // and owned_terrain_facet_flatness: the cliff projection and blend
    // steepness inputs), packed as the decode stores them.
    std::vector<std::uint32_t> slopes, flatness;
};
}
