#pragma once

// Terrain tile families from map placements (terrain_tile_blend, automatic
// source for atlases without editor data). A grid vertex shows one material,
// so the atlas-slot corners every map places around one vertex belong to one
// family. Pooled over all maps of an atlas (content + layout/types/animation), corner
// nodes are joined strongest link first; a link joining two groups must carry
// kTerrainTileMapSupport of the smaller group's vertex incidences, so a few
// mis-painted vertices cannot fuse two families. A group is named only from
// the ETERRTYPE of its uniform member slots (all four corners in the group);
// a group without one joins the named group it shares the most vertices
// with (the same support on the group total). Groups left unnamed and
// never-placed corners stay 0 (the tile keeps its art).
//
// Evidence (Prototypes/atlas-corners, shipped vanilla/JMV/Dejavu maps vs the
// editor presets and Dejavu profiles): shipped preset families are vertex-
// consistent on 99.7-100% of shared vertices under the decode corner rule;
// this grouping matches 98.7% of placed corners' families and names classes
// 96% right (stone vs ground is where ETERRTYPE and the editor disagree).

#include "terrain_tile_blend.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace owned {

// One placed base tile: world tile position, atlas slot, packed quarter turns.
struct TerrainTilePlacement { std::int32_t x, y; std::uint16_t slot; std::uint8_t rotation; };

constexpr float kTerrainTileMapSupport = 0.01f;

// Signature corner (NW, NE, SE, SW) at world corner cy*2+cx of a tile turned
// `rotation` quarter turns: the decode lane rule (owned_asset_loader
// terrain_uvs) through terrain_tile_world_corners, so both share one rule.
inline std::array<std::array<std::uint8_t, 4>, 4> terrain_tile_rotation_corners() {
    std::array<std::array<std::uint8_t, 4>, 4> out{};
    for (unsigned r = 0; r < 4; ++r) {
        std::array<std::array<float, 2>, 9> lane{};
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 3; ++x) lane[y * 3 + x] = {x * .5f, 1 - y * .5f};
        for (unsigned turn = 0; turn < r; ++turn) {
            const auto c = lane[0]; lane[0] = lane[2]; lane[2] = lane[8]; lane[8] = lane[6]; lane[6] = c;
        }
        const auto world = terrain_tile_world_corners({0, 1, 2, 3}, {lane[0], lane[2], lane[6], lane[8]});
        for (unsigned k = 0; k < 4; ++k) out[r][k] = std::uint8_t(world[k]);
    }
    return out;
}

// Every shared grid vertex of the pooled maps: the distinct corner nodes
// (slot*4 + signature corner) around it.
struct TerrainTileVertexRuns {
    std::vector<std::uint16_t> nodes;
    std::vector<std::uint32_t> begin{0};
    void add_map(const std::vector<TerrainTilePlacement>& tiles, std::uint32_t slots, const std::vector<bool>& skip) {
        static const auto table = terrain_tile_rotation_corners();
        std::vector<std::uint64_t> keys;
        keys.reserve(tiles.size() * 4);
        for (const auto& t : tiles) {
            if (t.slot >= slots || (t.slot < skip.size() && skip[t.slot]) || t.x < 0 || t.y < 0 || t.x > 0xfffe || t.y > 0xfffe) continue;
            for (unsigned w = 0; w < 4; ++w) {
                const std::uint64_t vx = std::uint64_t(t.x) + (w & 1), vy = std::uint64_t(t.y) + (w >> 1);
                keys.push_back((((vy << 16) | vx) << 16) | (std::uint64_t(t.slot) * 4u + table[t.rotation & 3u][w]));
            }
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        for (std::size_t i = 0; i < keys.size();) {
            std::size_t j = i;
            while (j < keys.size() && (keys[j] >> 16) == (keys[i] >> 16)) ++j;
            if (j - i > 1) {
                for (std::size_t k = i; k < j; ++k) nodes.push_back(std::uint16_t(keys[k] & 0xffffu));
                begin.push_back(std::uint32_t(nodes.size()));
            }
            i = j;
        }
    }
    std::size_t size() const { return begin.size() - 1; }
};

// Share of shared vertices whose assigned corners all name one family (a
// check that a namespace-matched preset fits these maps' atlas layout).
// Shipped vanilla/JMV/Dejavu/Legends maps fit their presets at 92.5-99.9%
// (lowest: JMV zone14, whose preset splits two stone families the maps mix,
// and zone17); a mirrored corner layout of the same atlases fits at most 88%.
// The preset binds at kTerrainTilePresetFit or with under 64 testable vertices.
constexpr double kTerrainTilePresetFit = 0.90;
inline double terrain_tile_vertex_consistency(const TerrainTileVertexRuns& runs, const TerrainTileSignatures& signatures,
                                              std::size_t& tested) {
    tested = 0;
    std::size_t agree = 0;
    for (std::size_t v = 0; v < runs.size(); ++v) {
        std::uint32_t first = 0; bool same = true; unsigned count = 0;
        for (std::uint32_t i = runs.begin[v]; i < runs.begin[v + 1]; ++i) {
            const std::uint32_t n = runs.nodes[i];
            const std::uint32_t f = n / 4u < signatures.size() ? signatures[n / 4u][n % 4u] : 0u;
            if (!f) continue;
            ++count;
            if (!first) first = f; else if (f != first) same = false;
        }
        if (count >= 2) { ++tested; agree += same; }
    }
    return tested ? double(agree) / double(tested) : 1.0;
}

// Material word of an ETERRTYPE for a family name (terrain_tile_material_class
// reads its first word); nullptr: no name (TERRTYPE_UNDEFINED, unknown values).
inline const char* terrain_tile_type_word(std::uint32_t type) {
    static const char* const words[16] = {"Grass", "Ground", "Stone", "Sand", "Ground", "Ground", "Water", "Paving",
                                          nullptr, "Snow", "Ice", "Grass", "Snow", "Lava", "Swamp", "Rock"};
    return type < 16 ? words[type] : nullptr;
}

struct TerrainTileMapFamilies {
    TerrainTileFamilies families;
    std::size_t placed = 0, named = 0; // corners placed on some map / given a named family
};

// Families of `slots` atlas slots from the pooled vertex runs; types: the
// map's ETERRTYPE per slot; skip: slots never grouped (animated).
inline TerrainTileMapFamilies terrain_tile_map_families(const TerrainTileVertexRuns& runs, std::uint32_t slots,
    const std::vector<std::uint32_t>& types, const std::vector<bool>& skip, float support = kTerrainTileMapSupport) {
    TerrainTileMapFamilies out;
    const std::uint32_t n = slots * 4u;
    std::vector<std::uint32_t> seen(n, 0u);
    std::unordered_map<std::uint32_t, std::uint32_t> links;
    for (std::size_t v = 0; v < runs.size(); ++v) {
        const std::uint32_t b = runs.begin[v], e = runs.begin[v + 1];
        for (std::uint32_t i = b; i < e; ++i) {
            if (runs.nodes[i] >= n) continue;
            ++seen[runs.nodes[i]];
            for (std::uint32_t j = i + 1; j < e; ++j)
                if (runs.nodes[j] < n) ++links[(std::uint32_t(runs.nodes[i]) << 16) | runs.nodes[j]];
        }
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> order(links.begin(), links.end());
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });
    std::vector<std::uint32_t> parent(n), weight(seen);
    for (std::uint32_t i = 0; i < n; ++i) parent[i] = i;
    const auto find = [&](std::uint32_t a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    for (const auto& [key, count] : order) {
        std::uint32_t a = find(key >> 16), b = find(key & 0xffffu);
        if (a == b || float(count) < support * float((std::min)(weight[a], weight[b]))) continue;
        if (b < a) std::swap(a, b);
        parent[b] = a; weight[a] += weight[b];
    }
    // Uniform member slots name their group (votes by material word; ties: first word).
    std::unordered_map<std::uint32_t, std::array<std::uint32_t, 16>> votes;
    std::vector<std::uint32_t> group_size(n, 0u);
    for (std::uint32_t i = 0; i < n; ++i) if (seen[i]) ++group_size[find(i)];
    for (std::uint32_t s = 0; s < slots; ++s) {
        if (s < skip.size() && skip[s]) continue;
        const std::uint32_t g = find(s * 4u);
        bool uniform = seen[s * 4u] && group_size[g] > 1;
        for (unsigned k = 1; k < 4 && uniform; ++k) uniform = seen[s * 4u + k] && find(s * 4u + k) == g;
        if (uniform && s < types.size() && terrain_tile_type_word(types[s])) ++votes[g][types[s] & 15u];
    }
    // A group without a uniform member slot (e.g. the stone corners of
    // sand/stone transitions on a cliff whose plain stone tiles sit elsewhere)
    // joins the named group it shares the most vertices with, when that total
    // carries `support` of the smaller weight. The pair rule above measures
    // each node pair alone, so a group tied to a family by many weak pairs
    // stayed unnamed and its tiles kept their art (zone19 2026-10-08: 167
    // stone corners, 1803 links to the stone family, none of 36 per pair).
    // Named groups never merge here, so their families are unchanged.
    for (bool merged = true; merged;) {
        merged = false;
        std::unordered_map<std::uint64_t, std::uint32_t> totals;
        for (const auto& [key, count] : links) {
            const std::uint32_t a = find(key >> 16), b = find(key & 0xffffu);
            if (a == b) continue;
            totals[(std::uint64_t(a) << 32) | b] += count;
            totals[(std::uint64_t(b) << 32) | a] += count;
        }
        std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> best;
        for (const auto& [key, count] : totals) {
            const std::uint32_t a = std::uint32_t(key >> 32), b = std::uint32_t(key);
            if (votes.count(a) || !votes.count(b) || float(count) < support * float((std::min)(weight[a], weight[b]))) continue;
            auto& slot = best[a];
            if (count > slot.second || (count == slot.second && b < slot.first)) slot = {b, count};
        }
        for (const auto& [a, target] : best) {
            if (find(a) != a) continue;
            parent[a] = target.first; weight[target.first] += weight[a]; merged = true;
        }
    }
    std::fill(group_size.begin(), group_size.end(), 0u);
    for (std::uint32_t i = 0; i < n; ++i) if (seen[i]) ++group_size[find(i)];
    std::unordered_map<std::uint32_t, std::uint32_t> family_of;
    out.families.signatures.assign(slots, {});
    for (std::uint32_t s = 0; s < slots; ++s)
        for (unsigned k = 0; k < 4; ++k) {
            const std::uint32_t node = s * 4u + k;
            if (!seen[node] || (s < skip.size() && skip[s])) continue;
            ++out.placed;
            const std::uint32_t g = find(node);
            const auto v = votes.find(g);
            if (group_size[g] < 2 || v == votes.end()) continue;
            auto f = family_of.find(g);
            if (f == family_of.end()) {
                std::uint32_t best = 0;
                for (std::uint32_t t = 1; t < 16; ++t) if (v->second[t] > v->second[best]) best = t;
                const std::uint32_t id = std::uint32_t(family_of.size()) + 1u;
                f = family_of.emplace(g, id).first;
                out.families.names[id] = std::string(terrain_tile_type_word(best)) + " / map group " + std::to_string(id);
                out.families.placement_names.insert(id);
            }
            out.families.signatures[s][k] = f->second;
            ++out.named;
        }
    // Inferred stone/ground: only a named hard family is a cliff's exposed rock.
    out.families.rock_hard_only = true;
    return out;
}

// One MPR map file's atlas binding and base tiles, read straight from its RES
// directory (only the .mp header and the .sec members). Map files are
// third-party data: every size and offset is checked; a malformed file is
// skipped. tiles == nullptr reads only the atlas binding metadata.
struct TerrainTileMapFile {
    std::string space;
    std::uint32_t slots = 0, pages = 0, texture_size = 0, tile_size = 0;
    std::vector<std::uint32_t> types, animation_phases;
};
inline bool terrain_tile_read_map_file(const std::wstring& path, TerrainTileMapFile& file,
                                       std::vector<TerrainTilePlacement>* tiles) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::uint64_t size = std::uint64_t(in.tellg());
    const auto read = [&](std::uint64_t offset, void* out, std::size_t bytes) {
        if (offset > size || bytes > size - offset) return false;
        in.seekg(std::streamoff(offset));
        return bool(in.read(static_cast<char*>(out), std::streamsize(bytes)));
    };
    std::uint32_t head[4]{};
    if (!read(0, head, sizeof(head)) || head[0] != 0x019CE23Cu || !head[1] || head[1] > 65536u || head[3] > (1u << 24)) return false;
    std::vector<std::uint8_t> table(std::size_t(head[1]) * 22u), names(head[3]);
    if (!read(head[2], table.data(), table.size()) || !read(std::uint64_t(head[2]) + table.size(), names.data(), names.size())) return false;
    struct Member { std::string name; std::uint32_t size, offset; };
    std::vector<Member> members;
    const Member* mp = nullptr;
    for (std::uint32_t i = 0; i < head[1]; ++i) {
        const std::uint8_t* e = table.data() + std::size_t(i) * 22u;
        std::uint32_t msize, offset, name_offset; std::uint16_t name_len;
        std::memcpy(&msize, e + 4, 4); std::memcpy(&offset, e + 8, 4); std::memcpy(&name_len, e + 16, 2); std::memcpy(&name_offset, e + 18, 4);
        if (name_offset > names.size() || name_len > names.size() - name_offset) return false;
        std::string name(reinterpret_cast<const char*>(names.data()) + name_offset, name_len);
        for (char& c : name) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        members.push_back({std::move(name), msize, offset});
    }
    for (const auto& m : members)
        if (m.name.size() > 3 && m.name.compare(m.name.size() - 3, 3, ".mp") == 0) { if (mp) return false; mp = &m; }
    std::uint8_t header[38]{};
    if (!mp || mp->size < sizeof(header) || !read(mp->offset, header, sizeof(header))) return false;
    std::uint32_t textures, texture_size, tile_size, type_count, animation_count;
    std::uint16_t object_count;
    std::memcpy(&textures, header + 16, 4); std::memcpy(&texture_size, header + 20, 4); std::memcpy(&tile_size, header + 28, 4);
    std::memcpy(&type_count, header + 24, 4); std::memcpy(&object_count, header + 32, 2); std::memcpy(&animation_count, header + 34, 4);
    if (!textures || textures > 4096u || !tile_size || texture_size % tile_size || !texture_size || texture_size / tile_size > 64u) return false;
    const std::uint64_t types_at = sizeof(header) + std::uint64_t(object_count) * 44u;
    const std::uint64_t animations_at = types_at + std::uint64_t(type_count) * 4u;
    if (!type_count || type_count > 65536u || animation_count > 65536u ||
        animations_at + std::uint64_t(animation_count) * 4u != mp->size) return false;
    const std::uint32_t side = texture_size / tile_size;
    file.space = mp->name.substr(0, mp->name.size() - 3);
    file.slots = textures * side * side;
    file.pages = textures; file.texture_size = texture_size; file.tile_size = tile_size;
    file.types.resize(type_count); file.animation_phases.assign(type_count, 0u);
    if (!read(std::uint64_t(mp->offset) + types_at, file.types.data(), file.types.size() * 4u)) return false;
    std::vector<std::array<std::uint16_t, 2>> animations(animation_count);
    if (animation_count && !read(std::uint64_t(mp->offset) + animations_at, animations.data(), animations.size() * 4u)) return false;
    for (const auto& animation : animations) {
        if (animation[0] >= type_count || std::uint32_t(animation[0]) + animation[1] > type_count) return false;
        file.animation_phases[animation[0]] = animation[1]; // native last assignment wins
    }
    if (!tiles) return true;
    tiles->clear();
    for (const auto& m : members) {
        const std::size_t n = m.name.size();
        if (n < 10 || m.name.compare(n - 4, 4, ".sec") != 0) continue;
        std::uint32_t sx = 0, sy = 0; bool digits = true;
        for (std::size_t i = n - 10; i < n - 4; ++i) {
            const char c = m.name[i];
            digits = digits && c >= '0' && c <= '9';
            (i < n - 7 ? sx : sy) = (i < n - 7 ? sx : sy) * 10u + std::uint32_t(c - '0');
        }
        std::uint8_t flags = 0;
        if (!digits || m.size < 5 || !read(std::uint64_t(m.offset) + 4u, &flags, 1)) continue;
        const std::uint32_t base = 5u + 33u * 33u * 8u * ((flags & 1u) ? 2u : 1u);
        std::uint16_t ids[256];
        if (m.size < base + sizeof(ids) || !read(std::uint64_t(m.offset) + base, ids, sizeof(ids))) continue;
        for (std::uint32_t i = 0; i < 256; ++i) {
            if (ids[i] == 0xffffu || (ids[i] & 0x3fffu) >= file.slots) continue;
            tiles->push_back({std::int32_t(sx * 16u + i % 16u), std::int32_t(sy * 16u + i / 16u), std::uint16_t(ids[i] & 0x3fffu), std::uint8_t(ids[i] >> 14)});
        }
    }
    return true;
}

} // namespace owned
