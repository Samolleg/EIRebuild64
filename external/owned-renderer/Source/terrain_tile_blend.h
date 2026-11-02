#pragma once

// Terrain material blend (terrain_tile_blend). EI terrain paints every 2 m
// tile from one atlas slot; a transition slot is a baked mask between two to
// four plain materials, so paths and shores show the same straight-edged
// shapes and tile seams over and over. The map editor's atlas profile names
// the material at each corner of every slot. With it, a transition tile is
// redrawn from its parent materials' own plain-fill art, read in world space.
// Which material shows comes from a smooth field over the shared grid
// vertices, looked up through a world-space domain warp, plus world noise and
// the materials' own texel brightness, so boundaries meander naturally and
// never follow the 2 m grid. Plain ground away from transitions, unknown
// tiles and long/animated tiles keep their art.
//
// Corner spaces (shipped-data evidence, Dejavu maps): the profile signature
// is (NW, NE, SE, SW) as the editor's tile preview shows the slot, and the
// preview top is the slot's smaller atlas v. A drawn tile's world corner
// (cx,cy) samples the atlas corner of its corner lane UV, so the family at a
// world corner is the signature corner on that side of the slot's UV centre.
// Checked against the editor's per-vertex material fields shipped beside 19
// Dejavu maps: every vertex matches; the vertical mirror does not.

#include "small_json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace owned {

// Four family ids per atlas slot, (NW, NE, SE, SW); 0 = unassigned.
using TerrainTileSignatures = std::vector<std::array<std::uint32_t, 4>>;
// A map's families and where they came from (for the one log line).
struct TerrainTileFamilies {
    TerrainTileSignatures signatures;
    std::map<std::uint32_t, std::string> names; // editor family names ("Snow / Dark")
    std::string source;
    // Families inferred from map placements: a cliff's exposed rock comes only
    // from a hard-class family, never from ground (blend row z bit 12).
    bool rock_hard_only = false;
};

// Material classes for the edge character (shader tables tileBlendClass*),
// from the first word of the editor family name; unlisted words are ground.
// tile_flat: man-made or flat-only ground (paving, slabs, still water). It
// blends like hard ground on walkable tiles but never shows on a cliff face
// (terrain_tile_blend.hlsl), and is never a cliff's exposed rock.
enum TerrainTileClass : std::uint32_t { tile_ground, tile_hard, tile_grass, tile_snow, tile_ice, tile_sand, tile_mud, tile_flat };
inline std::uint32_t terrain_tile_material_class(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    const std::string word = name.substr(0, name.find_first_of(" /"));
    static const std::pair<const char*, TerrainTileClass> table[] = {
        {"stone", tile_hard}, {"rock", tile_hard}, {"paving", tile_flat}, {"slabs", tile_flat},
        {"rubble", tile_hard}, {"lava", tile_hard}, {"water", tile_flat}, {"grass", tile_grass},
        {"snow", tile_snow}, {"ice", tile_ice}, {"sand", tile_sand}, {"swamp", tile_mud}, {"mud", tile_mud}};
    for (const auto& [key, value] : table) if (word == key) return value;
    return tile_ground;
}

// "id=name;id=name;" (terrain_tile_presets.h).
inline void terrain_tile_names_from_preset(const char* families, std::map<std::uint32_t, std::string>& out) {
    out.clear();
    const std::string text = families;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t eq = text.find('=', at), end = text.find(';', at);
        if (eq == std::string::npos || end == std::string::npos || eq > end) break;
        out[std::uint32_t(std::stoul(text.substr(at, eq - at)))] = text.substr(eq + 1, end - eq - 1);
        at = end + 1;
    }
}

inline bool terrain_tile_decode_base64(const char* text, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint32_t bits = 0;
    int count = 0;
    for (const char* c = text; *c; ++c) {
        int v = *c >= 'A' && *c <= 'Z' ? *c - 'A' : *c >= 'a' && *c <= 'z' ? *c - 'a' + 26 :
            *c >= '0' && *c <= '9' ? *c - '0' + 52 : *c == '+' ? 62 : *c == '/' ? 63 : *c == '=' ? -2 : -1;
        if (v == -2) break;
        if (v < 0) return false;
        bits = (bits << 6) | std::uint32_t(v);
        if ((count += 6) >= 8) { count -= 8; out.push_back(std::uint8_t(bits >> count)); }
    }
    return true;
}

// Built-in editor preset (terrain_tile_presets.h layout): one byte a corner.
inline bool terrain_tile_signatures_from_preset(const char* base64, unsigned slots, TerrainTileSignatures& out) {
    std::vector<std::uint8_t> bytes;
    if (!terrain_tile_decode_base64(base64, bytes) || bytes.size() != std::size_t(slots) * 4u) return false;
    out.assign(slots, {});
    for (unsigned s = 0; s < slots; ++s)
        for (unsigned k = 0; k < 4; ++k) out[s][k] = bytes[s * 4u + k];
    return true;
}

// The editor's per-map atlas profile (ei_atlas_profile_<namespace>_<hash>.json,
// cornerSpace tile-preview-nw-ne-se-sw-v1). Bound to the map by namespace and
// slot count; ambiguous or unassigned slots stay 0.
inline bool terrain_tile_signatures_from_profile(const std::string& json, const std::string& map_namespace,
                                                 unsigned slots, TerrainTileSignatures& out,
                                                 std::map<std::uint32_t, std::string>* names = nullptr) {
    try {
        const auto root = small_json::Reader(json).root();
        if (names) {
            names->clear();
            for (const auto& family : root.field("families").list())
                (*names)[std::uint32_t(family.field("id").num())] = family.field("name").str();
        }
        std::string name = root.field("mprNamespace").str(), wanted = map_namespace;
        for (auto* s : {&name, &wanted})
            for (char& c : *s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        if (name != wanted || root.field("cornerSpace").str() != "tile-preview-nw-ne-se-sw-v1" ||
            root.field("atlasSlotCount").num() != double(slots)) return false;
        out.assign(slots, {});
        for (const auto& slot : root.field("slots").list()) {
            const double index = slot.field("slot").num();
            if (!(index >= 0 && index < slots) || slot.field("assigned").text != "true" ||
                slot.field("ambiguous").text != "false") continue;
            const auto& g = slot.field("signature");
            std::array<std::uint32_t, 4> corners{};
            const char* keys[4] = {"northWest", "northEast", "southEast", "southWest"};
            bool valid = true;
            for (unsigned k = 0; k < 4; ++k) {
                const double id = g.field(keys[k]).num();
                valid = valid && id >= 1 && id <= 4294967295.0;
                corners[k] = valid ? std::uint32_t(id) : 0u;
            }
            if (valid) out[std::size_t(index)] = corners;
        }
        return true;
    } catch (...) {
        out.clear();
        return false;
    }
}

// Families at a drawn tile's world corners (cx,cy) -> index cy*2+cx, from the
// slot signature and the atlas UVs (u right, v down) of those corner lanes.
inline std::array<std::uint32_t, 4> terrain_tile_world_corners(const std::array<std::uint32_t, 4>& signature,
                                                               const std::array<std::array<float, 2>, 4>& uv) {
    float cu = 0, cv = 0;
    for (const auto& c : uv) { cu += c[0] * .25f; cv += c[1] * .25f; }
    std::array<std::uint32_t, 4> out{};
    for (unsigned k = 0; k < 4; ++k) {
        const bool east = uv[k][0] > cu, south = uv[k][1] > cv;
        out[k] = signature[south ? (east ? 2u : 3u) : (east ? 1u : 0u)];
    }
    return out;
}

// Atlas slots with a sand corner: any of the slot's four corner families whose
// editor name class is sand. Ground cover's beach rules (kelp, shells,
// conches) accept these tiles beside ETERRTYPE sand.
inline std::vector<bool> terrain_tile_sand_slots(const TerrainTileFamilies& families) {
    std::vector<bool> out(families.signatures.size(), false);
    for (std::size_t s = 0; s < families.signatures.size(); ++s)
        for (const std::uint32_t family : families.signatures[s]) {
            const auto name = families.names.find(family);
            if (family && name != families.names.end() && terrain_tile_material_class(name->second) == tile_sand) out[s] = true;
        }
    return out;
}

// Soil/paving/rock corners of each slot for ground cover: bit k is set when
// signature corner k (NW, NE, SE, SW) names a family of the ground or hard
// class, except water (shore grass stays). Unassigned corners are never
// bare. All four set marks a fully classified soil/hard slot (meadow cover
// suppressed); fewer mark a
// grass/road transition, where cover thins toward the bare corners.
inline std::vector<std::uint8_t> terrain_tile_bare_corners(const TerrainTileFamilies& families) {
    std::vector<std::uint8_t> out(families.signatures.size(), 0);
    for (std::size_t s = 0; s < families.signatures.size(); ++s)
        for (unsigned k = 0; k < 4; ++k) {
            const auto family = families.signatures[s][k];
            const auto name = families.names.find(family);
            if (!family || name == families.names.end()) continue;
            const auto type = terrain_tile_material_class(name->second);
            std::string word = name->second.substr(0, name->second.find_first_of(" /")); // "Water / Still"
            for (char& c : word) if (c >= 'A' && c <= 'Z') c = char(c + 32);
            const bool water = word == "water";
            if ((type == tile_ground || type == tile_hard || type == tile_flat) && !water) out[s] = std::uint8_t(out[s] | (1u << k));
        }
    return out;
}

// Soft (loose, dry) sand by material identity: the editor sand families of
// the Suslanger desert atlases. Wet, hard and mixed sands are not soft:
// Underwater, Gravel, Grass, Grainy, Irregular, the Gipat beach colours
// (Common, Dark pink, Light) and Paving / Sand ruins. Snow-trail-style
// deformation and sand piles key on this, never on the map or region.
inline bool terrain_tile_soft_sand(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return name == "sand / yellow" || name == "sand / dark" || name == "sand / common grayish";
}
// Per slot, bit k for signature corner k (NW, NE, SE, SW) of a soft sand family.
inline std::vector<std::uint8_t> terrain_tile_soft_sand_corners(const TerrainTileFamilies& families) {
    std::vector<std::uint8_t> out(families.signatures.size(), 0);
    for (std::size_t s = 0; s < families.signatures.size(); ++s)
        for (unsigned k = 0; k < 4; ++k) {
            const auto name = families.names.find(families.signatures[s][k]);
            if (families.signatures[s][k] && name != families.names.end() && terrain_tile_soft_sand(name->second))
                out[s] = std::uint8_t(out[s] | (1u << k));
        }
    return out;
}

// The plain-fill slot of each family: the uniform slot drawn most often on
// the map (ties: the lower slot), else its lowest uniform slot. `usable`
// excludes slots the blend cannot read (long, animated, past the atlas).
inline std::map<std::uint32_t, std::uint32_t> terrain_tile_plain_slots(const TerrainTileSignatures& signatures,
    const std::vector<std::uint32_t>& usage, const std::vector<bool>& usable) {
    std::map<std::uint32_t, std::uint32_t> best;
    for (std::uint32_t s = 0; s < signatures.size(); ++s) {
        const auto& g = signatures[s];
        if (!g[0] || g[1] != g[0] || g[2] != g[0] || g[3] != g[0] || s >= usable.size() || !usable[s]) continue;
        const auto found = best.find(g[0]);
        const auto use = [&](std::uint32_t slot) { return slot < usage.size() ? usage[slot] : 0u; };
        if (found == best.end() || use(s) > use(found->second)) best[g[0]] = s;
    }
    return best;
}

// Liquid-bed (underwater) families: materials the map paints under a liquid
// surface ("Sand / Underwater", "Rock / Underwater emerald"). A bed family
// never lends its art to a dry family and never shows on dry ground
// (terrain_tile_blend.hlsl fades bed cells above the waterline). It is named
// so by the editor, or (map-placement families have no such name) at least
// kTerrainTileBedShare of its placed corners lie under a liquid at rest on
// this map. The editor profiles' own "underwater" flag is not used: it marks
// every sand of the Dejavu zone7 profile, including the dry island sands.
constexpr float kTerrainTileBedShare = .9f;
inline bool terrain_tile_bed_name(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return name.find("underwater") != std::string::npos;
}

// Families with no plain slot of their own (transition-only families, or a
// uniform slot the map never draws) borrow the plain cell of a family of the
// same material word ("Sand", "Stone"), most used first, so their tiles still
// blend instead of keeping hard-edged art. A dry family never borrows a bed
// family's cell (nor a bed family a dry one): on the Dejavu river map the
// most drawn sand is "Sand / Underwater", and the dry "Sand / Dark pink" drew
// its blue bed art. Families with no such donor keep their art.
inline void terrain_tile_borrow_cells(const TerrainTileFamilies& families, const std::vector<std::uint32_t>& family_usage_order,
                                      std::map<std::uint32_t, std::uint32_t>& cells, const std::set<std::uint32_t>& bed = {});
// Shader cell of a slot: atlas position (6 bits), page (6 bits), the
// family's material class (3 bits) and bit 15 for a liquid-bed family.
constexpr std::uint32_t terrain_tile_blend_none = 0xffffu;
constexpr std::uint32_t terrain_tile_blend_bed = 0x8000u;
// Page 63 is never a terrain page in a cell: it marks a folded cell.
constexpr std::uint32_t terrain_tile_fold_page = 63u;
inline std::uint32_t terrain_tile_blend_cell(std::uint32_t slot, std::uint32_t material_class = tile_ground, bool bed = false) {
    const std::uint32_t cell = slot / 64u < terrain_tile_fold_page && material_class < 8u
        ? (slot % 64u) | ((slot / 64u) << 6) | (material_class << 12) | (bed ? terrain_tile_blend_bed : 0u) : terrain_tile_blend_none;
    return cell;
}

// Folded cells: a family with no plain slot of its own draws its own art from
// one corner quadrant of a transition slot the map draws, mirror-folded into
// a tile (terrainTileBlendCellFrom). save1162: zone9 has no plain "Stone /
// Light", drew borrowed "Stone / Grayish" art, and met zone8's light stone
// in a line along the shared edge. The cell's page is terrain_tile_fold_page
// and its position the table index; a table entry (slot 6+6 bits, quadrant =
// signature corner NW/NE/SE/SW in bits 12-13) fills one extra row of the
// contact tile grid. Per family: the drawn transition slot with most of its
// corners (more drawn first on a tie) and, with three, the corner opposite
// the odd one (the farthest from the other material).
inline void terrain_tile_fold_cells(const TerrainTileFamilies& families, const std::vector<std::uint32_t>& usage,
    const std::vector<bool>& usable, std::map<std::uint32_t, std::uint32_t>& cells, const std::set<std::uint32_t>& bed,
    std::size_t capacity, std::vector<std::uint32_t>& table) {
    table.clear();
    capacity = (std::min<std::size_t>)(capacity, 64u);
    struct Pick { std::uint32_t slot = 0, count = 0, use = 0, corner = 0; };
    std::map<std::uint32_t, Pick> best;
    for (std::uint32_t s = 0; s < families.signatures.size(); ++s) {
        if (s >= usable.size() || !usable[s] || s / 64u >= terrain_tile_fold_page) continue;
        const std::uint32_t use = s < usage.size() ? usage[s] : 0u;
        if (!use) continue;
        const auto& g = families.signatures[s];
        for (unsigned k = 0; k < 4; ++k) {
            const std::uint32_t f = g[k];
            if (!f || cells.count(f)) continue;
            std::uint32_t count = 0;
            for (unsigned j = 0; j < 4; ++j) count += g[j] == f;
            if (count == 4) continue;
            std::uint32_t corner = k;
            if (count == 3) for (unsigned j = 0; j < 4; ++j) if (g[j] != f) corner = (j + 2u) % 4u;
            auto& p = best[f];
            if (count > p.count || (count == p.count && use > p.use)) p = {s, count, use, corner};
        }
    }
    for (const auto& [f, p] : best) {
        if (table.size() >= capacity) break;
        const auto name = families.names.find(f);
        const std::uint32_t material_class = terrain_tile_material_class(name != families.names.end() ? name->second : std::string());
        cells[f] = std::uint32_t(table.size()) | (terrain_tile_fold_page << 6) | (material_class << 12) |
                   (bed.count(f) ? terrain_tile_blend_bed : 0u);
        table.push_back((p.slot % 64u) | ((p.slot / 64u) << 6) | (p.corner << 12));
    }
}

inline void terrain_tile_borrow_cells(const TerrainTileFamilies& families, const std::vector<std::uint32_t>& family_usage_order,
                                      std::map<std::uint32_t, std::uint32_t>& cells, const std::set<std::uint32_t>& bed) {
    const auto word_of = [&](std::uint32_t f) {
        const auto n = families.names.find(f);
        std::string w = n != families.names.end() ? n->second.substr(0, n->second.find_first_of(" /")) : std::string();
        for (char& c : w) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        return w;
    };
    std::map<std::uint32_t, bool> seen;
    for (const auto& g : families.signatures) for (const auto f : g) if (f) seen[f] = true;
    for (const auto& [f, unused] : seen) {
        (void)unused;
        if (cells.count(f)) continue;
        const std::string w = word_of(f);
        const bool f_bed = bed.count(f) != 0;
        for (const auto donor : family_usage_order) {
            const auto c = cells.find(donor);
            if (donor != f && c != cells.end() && !w.empty() && word_of(donor) == w && (bed.count(donor) != 0) == f_bed) {
                cells[f] = c->second;
                break;
            }
        }
    }
}
// Live Cliff blend options (renderer options, F10 "Cliff blend"): name,
// default and the range terrain_cliff_blend_values() clamps to. They become
// the native terrain frame's nativeTerrainTess.z (rock detail) and
// nativeCliffBlend[0..1] (terrain_tile_blend.hlsl). Prototypes/shader-lab
// tunes the same options by name.
struct TerrainCliffBlendOption { const char* name; float fallback, minimum, maximum; };
inline constexpr TerrainCliffBlendOption kTerrainCliffBlendOptions[] = {
    {"cliff_rock_detail", .6f, 0.f, 1.f},    {"cliff_steep_start", 40.f, 20.f, 75.f},
    {"cliff_steep_full", 60.f, 25.f, 85.f},  {"cliff_rock_amount", .55f, 0.f, 1.f},
    {"cliff_rock_softness", .15f, .02f, .4f}, {"cliff_rock_scale", 1.f, .3f, 4.f},
    {"cliff_streak_length", 2.f, 1.f, 5.f},  {"cliff_snow_rock", .35f, 0.f, 1.f},
    {"cliff_face_warp", .6f, 0.f, 2.f}};
// value(option): the configured value of one of kTerrainCliffBlendOptions.
template<class Value>
void terrain_cliff_blend_values(const Value& value, float& rock_detail, float cliff_blend[8]) {
    const auto& o = kTerrainCliffBlendOptions;
    const auto get = [&](unsigned i) { return (std::clamp)(float(value(o[i])), o[i].minimum, o[i].maximum); };
    rock_detail = get(0);
    const float start = get(1), full = (std::clamp)(float(value(o[2])), start + 1.f, o[2].maximum);
    constexpr float degrees = 3.14159265f / 180.f;
    cliff_blend[0] = .5f - get(3); cliff_blend[1] = get(4); cliff_blend[2] = get(5); cliff_blend[3] = get(6);
    cliff_blend[4] = std::cos(start * degrees); cliff_blend[5] = std::cos(full * degrees);
    cliff_blend[6] = get(7); cliff_blend[7] = get(8);
}

// Field reach: the warp moves a lookup at most 1.25 tiles and the kernel
// reads the vertex nearest to it and its neighbours, so a tile's pixels see
// vertices kTerrainTileBlendReach tiles out (and one more on the far side).
constexpr int kTerrainTileBlendReach = 2;

// Seam margin (update_terrain_tile_blend_seams, terrain_tile_seam_margin_count): past
// an edge another map continues, the field reads that map's vertices this
// many rings out (warp 1.25 tiles plus the kernel's vertex beyond the
// nearest). They fill kTerrainTileSeamMarginRows rows of the contact tile
// grid between the blend rows and the fold row, two 16-bit cells a lane, in
// the order the y<0 rings, the y>h rings (x from -R to w+R), the x<0 rings,
// the x>w rings (y from 0 to h). The shader's copy is contactMarginRows.
constexpr int kTerrainTileSeamMargin = 3;
constexpr unsigned kTerrainTileSeamMarginRows = 6;
inline std::size_t terrain_tile_seam_margin_count(unsigned width, unsigned height) {
    const std::size_t r = kTerrainTileSeamMargin;
    return 2 * r * (std::size_t(width) + 1 + 2 * r) + 2 * r * (std::size_t(height) + 1);
}
inline bool terrain_tile_seam_margin_fits(unsigned width, unsigned height) {
    return width > 0 && terrain_tile_seam_margin_count(width, height) <= std::size_t(kTerrainTileSeamMarginRows) * width * 8;
}

// Per tile of a width x height grid (the shader's rows [h/2,h) of the contact
// tile grid):
//   x = material cells of the tile's grid vertices (0,0) | (1,0) << 16,
//   y = vertices (0,1) | (1,1) << 16 (terrain_tile_blend_none: no material),
//   z = bit 0 known (blends; its art is one of the field's materials),
//       bit 1 plain (all four corners one material), bit 2 active (some
//       vertex within reach differs, so the field can change here; every
//       tile on the map border, where the field narrows to the edge
//       vertices an adjoining map shares instead of the tile art),
//       bit 3 steep (some face past the walk limit: the blend owns its side
//       projection, so the tile is active), bits 4-7 edge e (0 -x, 1 +x,
//       2 -y, 3 +y) toward a tile that is not known, bits 8-11 edge e toward
//       a plain tile,
//       bit 12 rock from hard only (families inferred from map placements:
//       a ground family is never the exposed rock),
//       bits 13-15 the tile's exposed rock as an index into its six cliff
//       cells (0-3 the corners x lo, x hi, y lo, y hi; 4 top; 5 bottom; 7
//       none), chosen here for every tile, unknown ones included,
//       bits 16-31 (steep tiles) the cell of the material at the top of the
//       slope (terrain_tile_blend_none on every other tile),
//   w = bits 0-7 all ones (never an art cell for the contact/cliff readers),
//       bits 8-11 vertex k (0,0) (1,0) (0,1) (1,1) is a single-vertex paint
//       (its material differs from all 8 neighbouring vertices: the shader
//       keeps a blob of it on the vertex),
//       bits 12-13 plain depth (tiles to the nearest tile that is not plain
//       of this tile's material, 0..3),
//       bits 16-31 (steep tiles) the cell of the material at the bottom
//       (terrain_tile_blend_none on every other tile).
// A face shows only materials of its own cliff: its tile's corners and the
// top and bottom materials; exposed rock is one of those (stone, else
// ground). `underwater` tiles (a corner under a liquid surface at rest) never
// give a top or bottom material.
// Family of every grid vertex ((width+1) x (height+1), row-major; 0: none)
// from the tiles around it whose four corner families are known: the family
// they agree on, or where they disagree the family most of them name (ties:
// the lower id), so those tiles still blend instead of keeping their
// hard-edged art.
inline std::vector<std::uint32_t> terrain_tile_vertex_families(std::uint32_t width, std::uint32_t height,
    const std::vector<std::array<std::uint32_t, 4>>& corners) {
    const std::uint32_t conflict = ~0u, vw = width + 1;
    std::vector<std::uint32_t> family(std::size_t(vw) * (height + 1), 0u);
    if (corners.size() != std::size_t(width) * height) return family;
    const auto has_families = [&](std::size_t t) {
        const auto& c = corners[t];
        return c[0] && c[1] && c[2] && c[3];
    };
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t t = std::size_t(y) * width + x;
            if (!has_families(t)) continue;
            for (unsigned k = 0; k < 4; ++k) {
                auto& v = family[std::size_t(y + (k >> 1)) * vw + x + (k & 1)];
                v = !v ? corners[t][k] : v == corners[t][k] ? v : conflict;
            }
        }
    // A vertex the tiles around it disagree on takes the family most of them
    // name (ties: the lower id), so those tiles still blend instead of
    // keeping their hard-edged art.
    for (std::uint32_t y = 0; y <= height; ++y)
        for (std::uint32_t x = 0; x <= width; ++x) {
            auto& v = family[std::size_t(y) * vw + x];
            if (v != conflict) continue;
            std::map<std::uint32_t, unsigned> votes;
            for (unsigned k = 0; k < 4; ++k) {
                const int tx = int(x) - int(k & 1), ty = int(y) - int(k >> 1);
                if (tx < 0 || ty < 0 || tx >= int(width) || ty >= int(height)) continue;
                const std::size_t t = std::size_t(ty) * width + tx;
                if (has_families(t)) ++votes[corners[t][(k & 1) | ((k >> 1) << 1)]];
            }
            unsigned best = 0;
            for (const auto& [f, n] : votes) if (n > best) { best = n; v = f; }
        }
    return family;
}

// A vertex carries a material when its family (terrain_tile_vertex_families,
// or the family a shared map edge gives it) has a plain cell; a tile is known
// when its four corner families are known and every one of its vertices
// carries a material. Every tile's x/y come from the same vertex values, so
// neighbours see one field. vertex_cells (terrain 2.0): each vertex's cell
// given by the caller (a family's plain variants as their own materials).
inline std::vector<std::array<std::uint32_t, 4>> terrain_tile_blend_rows(std::uint32_t width, std::uint32_t height,
    const std::vector<std::array<std::uint32_t, 4>>& corners, const std::vector<std::uint32_t>& family,
    const std::map<std::uint32_t, std::uint32_t>& cells,
    const std::vector<bool>& steep = {}, const std::vector<float>& heights = {},
    const std::vector<bool>& underwater = {}, bool rock_hard_only = false,
    const std::vector<std::uint32_t>& vertex_cells = {}) {
    // Unknown tiles: no flags, no top or bottom material (0xffff).
    const std::array<std::uint32_t, 4> none{~0u, ~0u, 0xffff0000u, ~0u};
    std::vector<std::array<std::uint32_t, 4>> rows(std::size_t(width) * height, none);
    const std::uint32_t vw = width + 1;
    if (corners.size() != rows.size() || family.size() != std::size_t(vw) * (height + 1)) return rows;
    const auto has_families = [&](std::size_t t) {
        const auto& c = corners[t];
        return c[0] && c[1] && c[2] && c[3];
    };
    std::vector<std::uint32_t> vertex(family.size(), terrain_tile_blend_none);
    if (vertex_cells.size() == family.size()) vertex = vertex_cells;
    else for (std::size_t i = 0; i < family.size(); ++i) {
        const auto found = family[i] ? cells.find(family[i]) : cells.end();
        if (found != cells.end()) vertex[i] = found->second;
    }
    const auto at = [&](int x, int y) {
        x = (std::clamp)(x, 0, int(width)); y = (std::clamp)(y, 0, int(height));
        return vertex[std::size_t(y) * vw + x];
    };
    std::vector<bool> known(rows.size(), false), plain(rows.size(), false);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t t = std::size_t(y) * width + x;
            const std::uint32_t v[4] = {at(int(x), int(y)), at(int(x) + 1, int(y)), at(int(x), int(y) + 1), at(int(x) + 1, int(y) + 1)};
            rows[t][0] = v[0] | (v[1] << 16);
            rows[t][1] = v[2] | (v[3] << 16);
            known[t] = has_families(t) && v[0] != terrain_tile_blend_none && v[1] != terrain_tile_blend_none &&
                v[2] != terrain_tile_blend_none && v[3] != terrain_tile_blend_none;
            plain[t] = known[t] && v[1] == v[0] && v[2] == v[0] && v[3] == v[0];
        }
    const int reach = kTerrainTileBlendReach;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t t = std::size_t(y) * width + x;
            if (!known[t]) continue;
            bool active = false;
            const std::uint32_t first = at(int(x), int(y));
            for (int j = -reach; !active && j <= reach + 1; ++j)
                for (int i = -reach; !active && i <= reach + 1; ++i) active = at(int(x) + i, int(y) + j) != first;
            const auto state = [&](int nx, int ny, const std::vector<bool>& set) {
                return nx >= 0 && ny >= 0 && nx < int(width) && ny < int(height) && set[std::size_t(ny) * width + nx];
            };
            const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
            const bool faces = t < steep.size() && steep[t];
            // Three tiles in (the shader's seam cross-fade band, tileBlendSeamBand).
            const bool border = x < 3 || y < 3 || x + 3 >= width || y + 3 >= height;
            std::uint32_t flags = 1u | (plain[t] ? 2u : 0u) | (active || faces || border ? 4u : 0u) | (faces ? 8u : 0u);
            for (unsigned e = 0; e < 4; ++e) {
                // The map edge is no unknown tile: the field runs to the edge
                // vertices an adjoining map shares (the shader narrows it there).
                const int ex = int(x) + dx[e], ey = int(y) + dy[e];
                const bool inside = ex >= 0 && ey >= 0 && ex < int(width) && ey < int(height);
                if (inside && !state(ex, ey, known)) flags |= 16u << e;
                if (state(int(x) + dx[e], int(y) + dy[e], plain)) flags |= 256u << e;
            }
            // Cliff edges: on a steep tile, the material where the slope ends
            // above it and below it (walking up and down the tile-height
            // gradient to the first walkable tile, at most six tiles). A face
            // shows only its own corners' materials and these two.
            std::uint32_t top = terrain_tile_blend_none, bottom = terrain_tile_blend_none;
            if (faces && heights.size() == rows.size()) {
                const auto hgt = [&](int nx, int ny) {
                    nx = (std::clamp)(nx, 0, int(width) - 1); ny = (std::clamp)(ny, 0, int(height) - 1);
                    return heights[std::size_t(ny) * width + nx];
                };
                float gx = hgt(int(x) + 1, int(y)) - hgt(int(x) - 1, int(y)), gy = hgt(int(x), int(y) + 1) - hgt(int(x), int(y) - 1);
                const float gl = std::sqrt(gx * gx + gy * gy);
                if (gl > 1e-4f) { gx /= gl; gy /= gl; }
                const auto edge = [&](float sign) {
                    int nx = int(x), ny = int(y);
                    for (int step = 1; step <= 6; ++step) {
                        nx = (std::clamp)(int(std::lround(float(x) + sign * gx * float(step))), 0, int(width) - 1);
                        ny = (std::clamp)(int(std::lround(float(y) + sign * gy * float(step))), 0, int(height) - 1);
                        if (!(std::size_t(ny) * width + nx < steep.size() && steep[std::size_t(ny) * width + nx])) break;
                    }
                    std::map<std::uint32_t, unsigned> count;
                    for (unsigned k = 0; k < 4; ++k) {
                        const std::uint32_t v = at(nx + int(k & 1), ny + int(k >> 1));
                        if (v != terrain_tile_blend_none) ++count[v];
                    }
                    std::uint32_t pick = terrain_tile_blend_none; unsigned best = 0;
                    for (const auto& [v, n] : count) if (n > best) { best = n; pick = v; }
                    // A slope end under a liquid surface (a river or lake bed)
                    // gives no material: the face never shows the bed.
                    const std::size_t end = std::size_t(ny) * width + nx;
                    return end < underwater.size() && underwater[end] ? terrain_tile_blend_none : pick;
                };
                if (gl > 1e-4f) { top = edge(1.f); bottom = edge(-1.f); }
            }
            rows[t][2] = flags | (rock_hard_only ? 4096u : 0u) | (top << 16);
            std::uint32_t lone = 0;
            for (unsigned k = 0; k < 4; ++k) {
                const int vx = int(x) + int(k & 1), vy = int(y) + int(k >> 1);
                const std::uint32_t v = at(vx, vy);
                bool alone = v != terrain_tile_blend_none && vx > 0 && vy > 0 && vx < int(width) && vy < int(height);
                for (int j = -1; alone && j <= 1; ++j)
                    for (int i = -1; alone && i <= 1; ++i) alone = (i == 0 && j == 0) || at(vx + i, vy + j) != v;
                if (alone) lone |= 256u << k;
            }
            // Plain depth (bits 12-13): tiles to the nearest tile that is not
            // plain of this tile's material (0 on non-plain tiles, at most 3;
            // tiles past the map edge count as the same). The shader
            // interpolates it between tile centres for the own-art share, so
            // that share fades in over a smooth contour inside plain areas.
            std::uint32_t depth = 0;
            if (plain[t]) {
                depth = 3;
                for (int r = 1; r <= 2 && depth == 3; ++r)
                    for (int j = -r; j <= r && depth == 3; ++j)
                        for (int i = -r; i <= r && depth == 3; ++i) {
                            if ((std::max)(std::abs(i), std::abs(j)) != r) continue;
                            const int nx = int(x) + i, ny = int(y) + j;
                            if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) continue;
                            const std::size_t n = std::size_t(ny) * width + nx;
                            if (!plain[n] || at(nx, ny) != first) depth = std::uint32_t(r);
                        }
            }
            rows[t][3] = 0xffu | lone | (depth << 12) | (bottom << 16);
        }
    // Exposed rock of every tile (unknown ones too: a face beside them reads
    // their row): among its six cliff cells (corners, then top and bottom on
    // steep tiles), the lowest stone cell, else the lowest ground cell unless
    // bit 12 limits rock to stone; never a liquid-bed cell. Its index goes to
    // z bits 13-15 (7: none), so the shader reads it instead of choosing.
    for (auto& row : rows) {
        const bool steep_tile = (row[2] & 8u) != 0;
        const std::uint32_t six[6] = {row[0] & 0xffffu, row[0] >> 16, row[1] & 0xffffu, row[1] >> 16,
                                      steep_tile ? row[2] >> 16 : terrain_tile_blend_none,
                                      steep_tile ? row[3] >> 16 : terrain_tile_blend_none};
        std::uint32_t rock = terrain_tile_blend_none, best = 9u, index = 7u;
        for (std::uint32_t i = 0; i < 6; ++i) {
            const std::uint32_t k = (six[i] >> 12) & 7u, priority = k == tile_hard ? 0u : (k == tile_ground && !(row[2] & 4096u) ? 1u : 9u);
            if (!(six[i] & terrain_tile_blend_bed) && priority < 9u && (priority < best || (priority == best && six[i] < rock))) {
                rock = six[i]; best = priority; index = i;
            }
        }
        row[2] = (row[2] & ~(7u << 13)) | (index << 13);
    }
    return rows;
}

// Inserted after the terrain cliff functions of the native pixel shader
// (contact tile grid, contact atlas, TerrainCliffCell taps, hex lattice).
// nativeTerrainCliff.z: blend rows present (rows [h/2,h) of contactTiles);
// nativeTerrainCliff.w: atlas padding of a slot as a fraction of its side.
inline constexpr char kTerrainTileBlendShader[] = "#if 0"
#include "shaders/terrain_tile_blend.hlsl"
"\n#endif\n";

} // namespace owned
