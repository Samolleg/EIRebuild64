#pragma once

// Fur and hair prototype (F10 fur_mode: 0 off, 1 shells, 2 strands, 3 by
// kind: creatures shells, hair strands).
//
// One policy table: which unit models grow fur, how long, and which skeleton
// nodes may grow it (by part role, from the shared EI part vocabulary). The
// renderer draws an additive pass over the owned figure's prepared world
// vertices (renderer_fur.cpp); this header holds only CPU policy and the
// per-assembly profile the submission attaches to a unit's records.
//
// Inference (needs the user's approval before anything final depends on it):
// - part roles from names: hd head, bd/hp/nc body, tl* tail, ll/rl/lh/rh N
//   limb chains whose highest index is the paw/hoof, chelust jaw (teeth),
//   box* helpers; unknown names grow nothing;
// - hr* parts on animated unit models are hair;
// - per-model coat lengths below (fractions of the model's envelope radius).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

enum FurRole : std::uint32_t { fur_role_none = 0, fur_role_body = 1, fur_role_head = 2, fur_role_tail = 3,
    fur_role_limb = 4, fur_role_hair = 5, fur_role_armour = 6 };

// Fur armour (user list item 9): strands on the texels of a strand material
// class (fur, pelt) of a character's composed atlas. The classes come from
// the pseudo-PBR S1 material library (material_library.h, claude/pbr-s1):
// without it in the build no part takes the armour role and the renderer
// names the omission once.
#if __has_include("material_library.h")
#define EID3D11_FUR_ARMOUR 1
#else
#define EID3D11_FUR_ARMOUR 0
#endif
// Armour coat length, a fraction of the character's largest part radius
// (inference, a starting value for the visual check).
inline constexpr float kFurArmourCoat = .05f;

// One skeleton node's fur: length in world units (0 grows nothing) and role.
// A hair node's border is the start of its vertex list in hair_border (-1:
// none, the strands are not clamped).
struct FurNodeGpu {
    float length = 0;
    float role = 0;
    float border = -1;
    float padding = 0;
};
static_assert(sizeof(FurNodeGpu) == 16, "FurNodeGpu must match the HLSL layout");

// Immutable per-assembly profile. Indexed by skeleton node.
struct FurProfile {
    std::vector<FurNodeGpu> nodes;
    std::uint32_t head_node = UINT32_MAX;   // "hd": body axis front, hair scalp centre
    std::uint32_t tail_node = UINT32_MAX;   // first tail node, else hips: body axis back
    std::array<float, 4> head_sphere{};     // head part bounds in head-node space: centre, radius
    bool head_sphere_valid = false;
    float radius = 0;                       // creature size (largest part's vertex radius, scaled): the LOD's measure
    bool fur = false;                       // any fur node
    bool hair = false;                      // any hair node
    bool armour = false;                    // any fur armour node (no creature coat)
    std::string model;                      // visual reference, for the one log line
    // Hair strands follow the hair mesh's painted texture direction: 0 along
    // U, 1 along V, whichever runs closer to the mesh's vertical (local +Z,
    // inference) over its area.
    std::uint32_t hair_axis = 1;
    // Hair meshes: each vertex's distance to the mesh's open border (an edge
    // of one triangle: trimmed helmet rims, hairlines), in its node's units at
    // the unit's shape, one list per hair node (FurNodeGpu::border). The fur
    // pass ends strands there, so they never reach past the mesh's own extent.
    std::vector<float> hair_border;
};

struct FurModelCoat {
    const char* model;   // substring of the model's visual reference
    float length;        // coat length as a fraction of the largest part's vertex radius
    bool head = true;    // the head grows fur (false: body only)
};

// Furry unit models (units.udb model names; Dejavu variants share the stem).
inline constexpr FurModelCoat kFurModels[] = {
    {"unanwiti", .080f},   // tiger: fluffy, long enough to cover part joins
    {"unanwiwo", .110f},   // wolf: long
    {"unanwibo", .080f},   // boar: bristle
    {"unanwihy", .080f},   // hyena
    {"unanhodo", .075f},   // dog
    // Dejavu fox: one mesh for the whole animal, so the length is a fraction of the
    // whole body (1.1 m), not of one part: .03 = about 3 cm, between dog and wolf.
    // Its region maps keep face, ears and legs short (hide_sleek). User review 2026-11-21.
    {"unmofo", .030f},
    {"unanwira", .065f},   // rat
    {"unanhoha", .075f},   // hare
    {"unanwide", .040f},   // deer: short coat
    {"unanhoho", .035f},   // horse: short coat
    {"unanhoco", .035f},   // cow: short coat
    {"unanhopi", .028f},   // pig: sparse
    // Bat: body only. Its head is mostly face, and the Dejavu bat's bones all
    // sit at the body origin (unanwiba.bon), so there is no head-tail axis for
    // the head-front rule: head fur covered the face. Wings and the Dejavu
    // limb names (l_hand1, r_leg1) are not in the role table.
    {"unanwiba", .040f, false},
};

// Role length factors: body and upper limbs 1 (the neck ruff 1.4, in the
// builder), head .6 (longer head fur smeared the face), tail 1.2. Equal body
// and limb lengths let the coat cover the part joins. Close factors keep part seams from showing as
// hard length steps (menu verdict 2026-10-29: a hard stop at the shoulder).
inline float fur_role_factor(FurRole role) noexcept {
    switch (role) {
    case fur_role_body: return 1.f;
    case fur_role_head: return .6f;
    case fur_role_tail: return 1.2f;
    case fur_role_limb: return 1.f;
    default: return 0.f;
    }
}

// A part name's role stem: lower case, up to the first '.'.
inline std::string fur_part_stem(const std::string& name) {
    std::string stem;
    for (const char c : name) {
        if (c == '.') break;
        stem.push_back(c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
    }
    return stem;
}
inline bool fur_hair_stem(const std::string& stem) noexcept { return stem.size() >= 2 && stem[0] == 'h' && stem[1] == 'r'; }

// Limb chain of a stem ("ll3" -> chain 0 "ll", index 3); -1 when not a limb.
inline int fur_limb_chain(const std::string& stem, int& index) noexcept {
    index = 0;
    if (stem.size() != 3 || stem[2] < '0' || stem[2] > '9') return -1;
    static constexpr const char* chains[] = {"ll", "rl", "lh", "rh"};
    for (int c = 0; c < 4; ++c)
        if (stem[0] == chains[c][0] && stem[1] == chains[c][1]) { index = stem[2] - '0'; return c; }
    return -1;
}

// Role of a non-limb stem (limbs are resolved against the assembly's chains).
inline FurRole fur_stem_role(const std::string& stem) noexcept {
    if (stem == "hd") return fur_role_head;
    if (stem == "bd" || stem == "hp" || stem == "nc") return fur_role_body;
    if (stem.size() == 3 && stem[0] == 't' && stem[1] == 'l' && stem[2] >= '0' && stem[2] <= '9') return fur_role_tail;
    return fur_role_none;   // chelust (jaw/teeth), box* helpers, anything unknown
}

extern int g_fur_mode;   // F10 fur_mode (owned_world_state.cpp)
// Owned party portraits: F10 portrait_head / portrait_helmet (owned_world_state.cpp).
extern bool g_owned_portraits;
extern bool g_portrait_helmet;

// Which kinds take strands: hair in modes 2 and 3, creature coats in mode 2
// only (user verdict 2026-10-31: shells look better on animals, strands on
// human hair; 1 and 2 stay for comparison).
inline bool fur_hair_strands(int mode) noexcept { return mode >= 2; }
inline bool fur_coat_strands(int) noexcept { return false; }  // creatures always take shells

// A unit's hair part in strand mode (fur_hair_strands): its own record (never
// merged: record flag 0x40 clear) whose node is a hair node. The fur pass
// draws it; only its colour record is hidden, its sun shadow caster keeps
// the part's alpha (renderer_native_queue.cpp).
inline bool fur_strand_hair_record(const FurProfile* profile, std::uint32_t node_base, std::uint32_t flags,
                                   int mode) noexcept {
    return fur_hair_strands(mode) && profile && (flags & 0x40u) == 0 && node_base < profile->nodes.size() &&
        profile->nodes[node_base].role == float(fur_role_hair);
}

}  // namespace owned
