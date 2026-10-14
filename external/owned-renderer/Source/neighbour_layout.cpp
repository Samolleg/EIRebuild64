#include "neighbour_layout.h"
#include "small_json.h"
#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace owned {
namespace {
using small_json::require;
using small_json::Value;
using small_json::Reader;
std::string identity(const Value& value) {
    auto s = neighbour_key(value.str());
    require(!s.empty() && s.size() <= 256 && s != "." && s != "..", "Invalid layout identity");
    for (unsigned char c : s) require(c >= 32 && c < 127 && c != '/' && c != '\\' && c != ':', "Unsupported layout identity");
    return s;
}
std::string id(const NeighbourPlacement& p) { return p.profile + '\x1f' + p.allod + '\x1f' + p.zone; }
bool complete(const NeighbourPlacement& p) { return !p.mpr_path.empty() && p.width > 0 && p.height > 0; }
}
std::string neighbour_key(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    s = s.substr(a, b - a + 1);
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}
std::vector<NeighbourPlacement> parse_neighbour_layout(const std::string& text) {
    const auto root = Reader(text).root();
    const double schema = root.field("schemaVersion").num();
    require((schema == 1 || schema == 2) && root.field("layoutAlgorithmVersion").num() == 1 &&
        root.field("coordinateSystem").str() == "mpr-local-xy", "Unsupported layout version/coordinates");
    std::vector<NeighbourPlacement> result;
    std::set<std::string> unique;
    for (const auto& profile : root.field("profiles").list())
        for (const auto& allod : profile.field("allods").list())
            for (const auto& zone : allod.field("zones").list()) {
                NeighbourPlacement p;
                p.profile = identity(profile.field("profileId")); p.allod = identity(allod.field("allodId"));
                p.zone = identity(zone.field("zoneId")); p.mpr = identity(zone.field("mprIdentity"));
                p.component = identity(zone.field("componentAnchorZoneId"));
                p.x = zone.field("origin").field("x").num(); p.y = zone.field("origin").field("y").num();
                if (zone.members.count("mprPath")) p.mpr_path = zone.field("mprPath").str();
                if (zone.members.count("mobPath")) p.mob_path = zone.field("mobPath").str();
                if (zone.members.count("sectorWidth") && zone.members.count("sectorHeight")) {
                    const double w = zone.field("sectorWidth").num(), h = zone.field("sectorHeight").num();
                    if (w >= 1 && w <= 128 && h >= 1 && h <= 128 && w == std::floor(w) && h == std::floor(h)) {
                        p.width = w * 32; p.height = h * 32;
                    }
                }
                require(unique.insert(id(p)).second, "Duplicate layout zone");
                result.push_back(std::move(p));
            }
    return result;
}
const NeighbourPlacement& neighbour_layout_active(const std::vector<NeighbourPlacement>& layout,
    const std::string& profile, const std::string& mpr, const std::string& allod, const std::string& zone) {
    const auto pr = neighbour_key(profile), map = neighbour_key(mpr), a = neighbour_key(allod), z = neighbour_key(zone);
    require(!pr.empty() && !map.empty(), "Missing active MPR identity");
    const NeighbourPlacement *active = nullptr, *hint = nullptr;
    std::size_t matches = 0, hints = 0;
    for (const auto& p : layout) if (p.profile == pr && p.mpr == map) {
        active = &p; ++matches;
        if ((!a.empty() || !z.empty()) && (a.empty() || a == p.allod) && (z.empty() || z == p.zone)) { hint = &p; ++hints; }
    }
    if (matches > 1 && hints == 1) active = hint;
    else require(matches == 1, matches ? "Ambiguous active MPR in saved layout" : "Active MPR absent from saved layout");
    require(complete(*active), "Missing active catalog entry");
    return *active;
}
std::vector<NeighbourSelection> select_neighbour_layout(const std::vector<NeighbourPlacement>& layout,
    const std::string& profile, const std::string& mpr, const std::string& allod, const std::string& zone) {
    const NeighbourPlacement* active = &neighbour_layout_active(layout, profile, mpr, allod, zone);
    std::vector<NeighbourSelection> result;
    for (const auto& p : layout) {
        if (p.profile != active->profile || p.allod != active->allod || p.component != active->component ||
            id(p) == id(*active) || p.mpr == active->mpr || !complete(p)) continue;
        const double x = p.x - active->x, y = p.y - active->y, right = x + p.width, top = y + p.height;
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(right) && std::isfinite(top) &&
            x <= active->width && right >= 0 && y <= active->height && top >= 0) result.push_back({p, x, y});
    }
    std::sort(result.begin(), result.end(), [](const auto& l, const auto& r) { return id(l.placement) < id(r.placement); });
    return result;
}
NeighbourBounds neighbour_fog_edges(const NeighbourBounds& piece,
    const std::vector<NeighbourBounds>& rendered) {
    NeighbourBounds edges = piece;
    // side: 0 west, 1 east, 2 south (y0), 3 north (y1).
    for (int side = 0; side < 4; ++side) {
        const bool along_y = side < 2;
        const double from = along_y ? piece.y0 : piece.x0, to = along_y ? piece.y1 : piece.x1;
        std::vector<std::pair<double, double>> spans;
        double outer_edge = 0;
        for (const auto& r : rendered) {
            const double contact = side == 0 ? r.x1 - piece.x0 : side == 1 ? r.x0 - piece.x1 :
                side == 2 ? r.y1 - piece.y0 : r.y0 - piece.y1;
            const double a = (std::max)(from, along_y ? r.y0 : r.x0), b = (std::min)(to, along_y ? r.y1 : r.x1);
            if (contact != 0 || !(b > a)) continue;
            const double outer = side == 0 ? r.x0 : side == 1 ? r.x1 : side == 2 ? r.y0 : r.y1;
            outer_edge = spans.empty() ? outer : (side == 0 || side == 2) ? (std::max)(outer_edge, outer) : (std::min)(outer_edge, outer);
            spans.push_back({a, b});
        }
        std::sort(spans.begin(), spans.end());
        double reach = from;
        for (const auto& span : spans) if (span.first <= reach) reach = (std::max)(reach, span.second);
        if (spans.empty() || reach < to) continue;
        (side == 0 ? edges.x0 : side == 1 ? edges.x1 : side == 2 ? edges.y0 : edges.y1) = outer_edge;
    }
    return edges;
}
NeighbourContacts neighbour_fog_contacts(const NeighbourBounds& piece,
    const std::vector<NeighbourBounds>& rendered) {
    NeighbourContacts contacts;
    for (int side = 0; side < 4; ++side) {
        const bool along_y = side == 0 || side == 2;
        const double from = along_y ? piece.y0 : piece.x0, to = along_y ? piece.y1 : piece.x1;
        const double line = side == 0 ? piece.x0 : side == 1 ? piece.y0 : side == 2 ? piece.x1 : piece.y1;
        // Rendered ground just on the piece's own side of the seam line (it
        // included) and just across it, 0.5 m off the line. Other pieces
        // along the same line count too, so a run continues past the piece's
        // corner where a second neighbour carries on beside the first (Gipat
        // zone7 west: zone1 touches zone7 and then zone3obr along x=0), and
        // so does a piece that spans the line beyond the corner (zone1's
        // north-east corner: zone2 above it, zone3obr across x=352 from y=64
        // to 384, so the band there was fog over the corner).
        const double outward = side < 2 ? -0.5 : 0.5;
        std::vector<std::pair<double, double>> inner{{from, to}}, outer, spans;
        for (const auto& r : rendered) {
            const std::pair<double, double> run{along_y ? r.y0 : r.x0, along_y ? r.y1 : r.x1};
            const double a0 = along_y ? r.x0 : r.y0, a1 = along_y ? r.x1 : r.y1;
            if (line - outward >= a0 && line - outward <= a1) inner.push_back(run);
            if (line + outward >= a0 && line + outward <= a1) outer.push_back(run);
        }
        for (const auto& a : inner) for (const auto& b : outer) {
            const double lo = (std::max)(a.first, b.first), hi = (std::min)(a.second, b.second);
            if (hi > lo) spans.push_back({lo, hi});
        }
        std::sort(spans.begin(), spans.end());
        // The merged run sharing most of the piece's own side.
        double best = 0;
        for (std::size_t i = 0; i < spans.size();) {
            double lo = spans[i].first, hi = spans[i].second;
            for (++i; i < spans.size() && spans[i].first <= hi; ++i) hi = (std::max)(hi, spans[i].second);
            const double shared = (std::min)(hi, to) - (std::max)(lo, from);
            if (shared > best) {best = shared;contacts.lo[side] = lo;contacts.hi[side] = hi;}
        }
    }
    return contacts;
}
}
