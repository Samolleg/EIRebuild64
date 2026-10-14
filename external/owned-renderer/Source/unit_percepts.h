#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace owned::critters {

// Supported image: 55FAE0/403F40 resolve the replicated signed ID through
// world+40, manager buckets [+20,+24), node {next,key,value} at {0,4,8}.
// 58DC93 establishes server mode +138 (1 = single player); 553519 establishes
// stats at specific+1E0, and 522ED3/522EFB establish percept +E4+8*i.
// The same stats hold the senses six slots earlier (+B4+8*i, effective
// values; 5536C1 reads INFRA at +29C), and 5536F3 stores the vision sense
// modifier (night/infravision, pose) at +904: the unit sees
// sense[SIGHT] x modifier metres. 5300D8 establishes the unit's controlling
// human player at +24C (null for every AI unit).
// Read at each publication, retaining neither engine pointers nor a lookup cache.
template<class Read>
const char* read_local_unit_percepts(const void* image, bool verified,
    std::int32_t id, float& visibility, float& loudness, Read read,
    float* vision = nullptr, bool* controlled = nullptr) noexcept {
    static_assert(sizeof(void*) == 4, "supported game ABI is x86");
    if (!verified || !image) return "server percept contract mismatch";
    const auto* world = static_cast<const std::uint8_t*>(image) + 0x3aff60;
    const auto get = [&](const void* base, std::size_t offset, auto& value) {
        return read(base, offset, &value, sizeof(value));
    };
    std::uint32_t mode = 0;
    if (!get(world, 0x138, mode)) return "server mode unreadable";
    if (mode != 1) return "no active single-player server";
    const void* table = nullptr;
    const void* manager = nullptr;
    const void* owner = nullptr;
    const auto* game = static_cast<const std::uint8_t*>(image);
    if (!get(world, 0, table) || table != game+0x33f928 ||
        !get(world, 0x40, manager) || !manager ||
        !get(manager, 0, table) || table != game+0x33dcd8 ||
        !get(manager, 0xac, owner) || owner != world)
        return "server manager ownership mismatch";
    std::uintptr_t begin = 0, end = 0;
    std::uint32_t count = 0;
    if (!get(manager, 0x20, begin) || !get(manager, 0x24, end) ||
        !get(manager, 0x40, count) || !begin || end <= begin || (end-begin)%4)
        return "server unit table unreadable";
    const auto bucket = static_cast<std::uint32_t>(id) % ((end-begin)/4);
    const void* node = nullptr;
    if (!get(reinterpret_cast<const void*>(begin), bucket*4, node))
        return "server unit bucket unreadable";
    // The manager's authoritative live-object count bounds traversal, including
    // malformed cycles. No fixed unit limit or per-frame allocation is needed.
    for (std::uint32_t visited = 0; node && visited < count; ++visited) {
        std::int32_t key = 0;
        if (!get(node, 4, key)) return "server unit node unreadable";
        if (key == id) {
            const void* unit = nullptr;
            const void* specific = nullptr;
            std::int32_t unit_id = 0;
            if (!get(node, 8, unit) || !unit ||
                !get(unit, 0, table) || table != game+0x33ea8c ||
                !get(unit, 0xc, unit_id) || unit_id != id ||
                !get(unit, 0x14, owner) || owner != manager ||
                !get(unit, 0x240, specific) || !specific ||
                !get(specific, 0, table) || table != game+0x33ef38 ||
                !get(specific, 0x1c, owner) || owner != unit)
                return "server unit link mismatch";
            float sight = 0, hearing = 0;
            if (!get(specific, 0x1e0+0xe4, sight) ||
                !get(specific, 0x1e0+0xe4+8*3, hearing) ||
                !std::isfinite(sight) || !std::isfinite(hearing) || sight < 0 || hearing < 0)
                return "server unit percepts invalid";
            float sense = 0, modifier = 0;
            const void* player = nullptr;
            if (vision && (!get(specific, 0x1e0+0xb4, sense) || !get(specific, 0x904, modifier) ||
                !std::isfinite(sense) || !std::isfinite(modifier) || sense < 0 || modifier < 0))
                return "server unit vision invalid";
            if (controlled && !get(unit, 0x24c, player)) return "server unit owner unreadable";
            if (vision) *vision = sense * modifier;
            if (controlled) *controlled = player != nullptr;
            visibility = sight;
            loudness = hearing;
            return nullptr;
        }
        if (!get(node, 0, node)) return "server unit chain unreadable";
    }
    return node ? "server unit chain exceeds live count" : "server unit ID absent";
}

} // namespace owned::critters
