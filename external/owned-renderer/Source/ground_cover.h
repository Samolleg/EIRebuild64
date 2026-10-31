#pragma once
// Renderer-owned procedural ground cover: the placement rule table, quality
// levels and the unit interaction law. No D3D and no engine state; the
// renderer (renderer_ground_cover.cpp) uploads the table once and places
// every instance on the GPU.
//
// Look: the approved offline prototype "C-v4" (claude/grass-prototype,
// Prototypes/ground_cover). Its per-ETERRTYPE rules are mapped onto the area
// classes of ambient_particles.h, the one classifier shared with the ambient
// particles (terrain type, liquid layer, leafy-tree count, zone class).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "ambient_particles.h"
#include "neighbour_fog.h"

namespace owned::ground_cover {

using ambient::AreaClass;
using ambient::ZoneClass;

enum Mesh : std::uint32_t {
    gm_blades = 0,   // tuft of curved blades (lush and dry grass)
    gm_flower,
    gm_pebble,
    gm_stone,
    gm_twig,
    gm_twig_fork,
    gm_twig_broken,  // two pieces with pale break faces
    gm_leaf,         // leafy deciduous trees only
    gm_needles,
    gm_log,
    gm_dead_bush,
    gm_reed,
    gm_cattail,
    gm_mound,        // snow pile
    gm_shell,
    gm_conch,
    gm_kelp,         // wrack on beaches
    gm_seagrass,     // under the sea
    gm_blades_far,   // fewer, wider blades for distant tufts
    gm_cone,
    gm_patch, gm_fern, gm_roots, gm_mushroom, gm_slab,
    gm_bones, gm_web, gm_crystal, gm_shard, gm_skull,
    // Far meshes of the tall and dense tufts: fewer, wider leaves and fewer
    // segments where the near leaves would be about a pixel wide.
    gm_flower_far, gm_reed_far, gm_cattail_far, gm_seagrass_far,
    gm_count,
};

// Bits of GroundCoverRule::flags.
enum RuleFlag : std::uint32_t {
    rf_tuft = 1,          // bends with wind/units; slope limit; thins under canopies and on beaches
    rf_snow_pile = 2,     // needs painted snow all around
    rf_shore_tangent = 4, // yaw along the shoreline
    rf_depth_clamp = 8,   // stays below the water surface
    rf_rock = 16,        // stone, rock, road and highrock base tiles
    rf_shore_clumps = 32, // shore kelp: only inside irregular clumps, waterline distance jittered
    rf_sand_pile = 64,    // with rf_snow_pile: a soft sand pile, sand base tiles all around
};
enum Field : std::uint32_t { fd_none = 0, fd_swamp_edge, fd_beach, fd_river, fd_depth,
    fd_lava_edge, fd_damp, fd_wall, fd_undead, fd_corner };
enum Palette : std::uint32_t { pal_none = 0, pal_flower, pal_leaf, pal_stone };
// Material art in the pixel shader: procedural colour, the game's tree bark /
// end grain, the placed stone props' rock art, snow pile.
enum Material : std::uint32_t { mat_plain = 0, mat_bark, mat_stone, mat_snow, mat_ground, mat_wet, mat_glow,
    mat_strand, mat_wrack }; // painted, alpha-cut strands (procedural, own pixel kind 4): seagrass; beach wrack

// The decoded loader keys are ASCII-folded. Both base and HD packs preserve
// these square atlas layouts; normalized crops do not depend on pixel size.
inline bool art_texture_matches(const std::string& key, const char* name,
                                std::uint32_t width, std::uint32_t height) {
    return width > 0 && width == height && (key == name || key == std::string(name) + ".mmp");
}

constexpr std::uint32_t area_bit(AreaClass a) noexcept { return 1u << a; }
constexpr std::uint32_t zone_bit(ZoneClass z) noexcept { return 1u << z; }
constexpr std::uint32_t kAllZones = (1u << ambient::zone_count) - 1u;
constexpr std::uint32_t kOutdoorZones = zone_bit(ambient::zone_outdoor) | zone_bit(ambient::zone_snow) |
                                        zone_bit(ambient::zone_desert);
constexpr std::uint32_t kLand = area_bit(ambient::area_bare) | area_bit(ambient::area_grass) |
                                area_bit(ambient::area_dry) | area_bit(ambient::area_sand) |
                                area_bit(ambient::area_snow);

// One placement rule. Mirrors the HLSL GcRule (structured buffer, 160 bytes).
struct GroundCoverRule {
    std::uint32_t mesh = gm_blades, areas = 0, zones = kOutdoorZones, field = fd_none;
    float density = 0, scale_min = 1, scale_max = 1, jitter = .1f;   // per m2; metres
    float sx_min = 1, sx_max = 1, sz_min = 1, sz_max = 1;           // footprint / height stretch
    float tint[3]{1, 1, 1}; float own_mix = 0;                      // terrain-colour multiplier
    float own[3]{1, 1, 1}; float frost = 0;                         // own colour, frosting
    float clump_freq = .3f, clump_lo = 0, clump_hi = 0, near_trees = 0;
    float align = 0, normal_blend = 0, sink = 0, contact = 0;       // contact: ground colour at the base
    float blend_height = 0, material = mat_plain, lod_near = 25, lod_far = 55;
    float field_a = 0, field_b = 0, palette = pal_none; std::uint32_t flags = 0;
    // Far LOD: density kept beyond lod_far (to the edge of the visible ground,
    // never faded out) and the largest growth of thinned survivors.
    float far_keep = .08f, grow_max = 1.5f;
    // Clustered placement (Cluster; shape in the shader's gcClusterShape) and
    // burial: the largest buried fraction of a tuft's full height (the
    // smallest is .2 less), so only the tips show above snow.
    float cluster = 0, bury = 0;
};
// Clustered rules move every candidate into the patch of its world-space
// lattice cell (ground_cover_interaction.h gcPatchAt) or drop it.
enum Cluster : std::uint32_t { cl_none = 0, cl_flower_patch, cl_snow_clump };
static_assert(sizeof(GroundCoverRule) == 160, "GroundCoverRule must match the HLSL GcRule");

// Per-mesh defaults from the prototype's R(): orientation, contact blend and
// distance thinning (metres from the eye; survivors grow as density thins).
inline GroundCoverRule make_rule(std::uint32_t mesh, std::uint32_t areas, float density, float smin, float smax) noexcept {
    GroundCoverRule r{};
    r.mesh = mesh; r.areas = areas; r.density = density; r.scale_min = smin; r.scale_max = smax;
    switch (mesh) {
    case gm_shell: case gm_conch:
        r.align = 1; r.normal_blend = .2f; r.lod_near = 35; r.lod_far = 100; r.contact = .3f; r.blend_height = .12f; r.sink = .1f; break;
    case gm_kelp:
        r.material = mat_wrack; r.align = 1; r.normal_blend = .3f; r.lod_near = 45; r.lod_far = 130; r.contact = .35f; r.blend_height = .03f;
        r.sx_max = 1.2f; r.flags = rf_shore_tangent; break;
    case gm_seagrass:
        r.material = mat_strand; r.align = .3f; r.normal_blend = .5f; r.sz_min = .8f; r.sz_max = 1.4f; r.lod_near = 50; r.lod_far = 140;
        r.flags = rf_tuft | rf_depth_clamp; break;
    case gm_blades: case gm_flower:
        r.align = .25f; r.normal_blend = .75f; r.sz_min = .8f; r.sz_max = 1.25f; r.lod_near = 40; r.lod_far = 110;
        r.flags = rf_tuft; break;
    case gm_reed: case gm_cattail:
        r.align = .15f; r.normal_blend = .6f; r.sz_min = .85f; r.sz_max = 1.3f; r.lod_near = 55; r.lod_far = 150;
        r.flags = rf_tuft; break;
    case gm_pebble:
        r.material = mat_stone; r.contact = .85f; r.blend_height = .55f; r.align = 1; r.normal_blend = .2f; r.sink = .25f;
        r.sx_min = .7f; r.sx_max = 1.4f; r.sz_min = .7f; r.sz_max = 1.2f; r.lod_near = 30; r.lod_far = 90; r.palette = pal_stone; break;
    case gm_stone:
        r.material = mat_stone; r.contact = .8f; r.blend_height = .5f; r.align = 1; r.normal_blend = .1f; r.sink = .3f;
        r.sx_min = .8f; r.sx_max = 1.3f; r.sz_min = .6f; r.sz_max = 1.3f; r.lod_near = 60; r.lod_far = 160; r.palette = pal_stone; break;
    case gm_twig: case gm_twig_fork: case gm_twig_broken:
        r.material = mat_bark; r.align = 1; r.normal_blend = .3f; r.lod_near = 35; r.lod_far = 100; r.sx_min = .8f; r.sx_max = 1.25f;
        r.contact = .6f; r.blend_height = .08f; break;
    case gm_leaf: case gm_needles:
        r.align = 1; r.normal_blend = .3f; r.lod_near = 35; r.lod_far = 100; r.sx_min = .8f; r.sx_max = 1.25f;
        r.contact = .35f; r.blend_height = .03f; r.palette = float(mesh == gm_leaf ? pal_leaf : pal_none); break;
    case gm_cone:
        r.material = mat_bark; r.align = 1; r.contact = .6f; r.blend_height = .08f;
        r.lod_near = 35; r.lod_far = 100; r.far_keep = .25f; break;
    case gm_patch:
        r.align = 1; r.contact = .85f; r.blend_height = .03f; r.lod_near = 40; r.lod_far = 120; r.far_keep = .5f; break;
    case gm_fern:
        r.contact = .8f; r.blend_height = .25f; r.normal_blend = .3f;
        r.lod_near = 45; r.lod_far = 130; r.far_keep = .5f; break;
    case gm_roots:
        r.material = mat_bark; r.contact = .7f; r.blend_height = .3f;
        r.lod_near = 60; r.lod_far = 160; r.far_keep = 1; break;
    case gm_mushroom:
        r.material = mat_glow; r.contact = .65f; r.blend_height = .1f;
        r.lod_near = 45; r.lod_far = 130; r.far_keep = .5f; break;
    case gm_slab: case gm_shard:
        r.material = mat_ground; r.align = 1; r.contact = .75f; r.blend_height = .1f; r.sink = .08f;
        r.lod_near = 45; r.lod_far = 130; r.far_keep = .5f; break;
    case gm_bones: case gm_skull:
        r.align = 1; r.contact = .6f; r.blend_height = .12f; r.own_mix = 1;
        r.own[0] = .72f; r.own[1] = .69f; r.own[2] = .56f;
        r.lod_near = 45; r.lod_far = 130; r.far_keep = .5f; break;
    case gm_web:
        r.contact = .65f; r.blend_height = .15f; r.own_mix = 1;
        r.own[0] = .52f; r.own[1] = .5f; r.own[2] = .44f;
        r.lod_near = 45; r.lod_far = 130; r.far_keep = .5f; break;
    case gm_crystal:
        r.contact = .75f; r.blend_height = .16f; r.align = 1;
        r.lod_near = 45; r.lod_far = 130; r.far_keep = .5f; break;
    case gm_log:
        r.material = mat_bark; r.contact = .6f; r.blend_height = .15f; r.align = 1; r.normal_blend = .1f; r.sink = .2f;
        r.lod_near = 70; r.lod_far = 180; break;
    case gm_dead_bush:
        r.material = mat_bark; r.contact = .5f; r.blend_height = .15f; r.align = .3f; r.normal_blend = .2f;
        r.sz_min = .8f; r.sz_max = 1.2f; r.lod_near = 70; r.lod_far = 180; break;
    case gm_mound:
        r.material = mat_snow; r.align = 1; r.normal_blend = .7f; r.sx_min = 1; r.sx_max = 1.8f; r.sz_min = .8f; r.sz_max = 1.2f;
        r.contact = 1; r.lod_near = 70; r.lod_far = 180; r.flags = rf_snow_pile; break;
    default: break;
    }
    switch (mesh) { // far LOD: what still reads at the farthest gameplay zoom
    case gm_blades: r.far_keep = .25f; r.grow_max = 2.0f; break;
    case gm_flower: r.far_keep = .15f; r.grow_max = 1.6f; break;
    case gm_reed: case gm_cattail: r.far_keep = .35f; r.grow_max = 1.6f; break;
    case gm_seagrass: r.far_keep = .3f; r.grow_max = 1.6f; break;
    case gm_kelp: case gm_shell: case gm_conch: r.far_keep = .25f; break;
    case gm_leaf: r.far_keep = .2f; break;
    case gm_needles: r.far_keep = .15f; break;
    case gm_pebble: r.far_keep = .12f; break;
    case gm_twig: case gm_twig_fork: case gm_twig_broken: r.far_keep = .25f; break;
    case gm_stone: r.far_keep = .5f; break;
    case gm_log: case gm_dead_bush: case gm_mound: r.far_keep = 1; break;
    default: break; // small debris thins but remains throughout visible ground
    }
    return r;
}

// The rule table: C-v4 with the user's review decisions. Lush grass ("C");
// leaves near trees; small stones; broken twigs; reeds and
// rotten wood in a band around swamps (nothing on the swamp itself); kelp and
// shells on open-water shores and seagrass under open water (never rivers);
// snow piles in Ingos only. No leaves on snow tiles anywhere; the snow region
// keeps only sparse dull-brown leaves on its bare ground and sparse dark,
// snow-dusted needles and twigs under conifers on snow.
// Tree-grid texel centres are two metres apart. Keep the existing six-metre
// neighbourhood, but let litter thin smoothly toward its edge.
inline float tree_litter_falloff(float dx, float dy) noexcept {
    const float t = std::clamp(std::sqrt(dx * dx + dy * dy) / 6.0f, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

// RGB: leafy broadleaf, conifer, non-conifer wood. Keep the last channel
// separate so bare-tree leaves in Dead City never borrow a fir's influence.
// Alpha belongs to cave burial sites and must survive tree accumulation.
inline void add_tree_litter_influence(std::array<std::uint8_t, 4>& value,
                                      ambient::TreeKind kind, float influence) noexcept {
    if (kind == ambient::tree_none) return;
    const auto add = [influence](std::uint8_t& v) {
        v = std::uint8_t(std::min(unsigned(v) + unsigned(85.f * influence + .5f), 255u));
    };
    if (kind == ambient::tree_deciduous) add(value[0]);
    if (kind == ambient::tree_conifer) add(value[1]);
    else add(value[2]);
}

inline const std::array<GroundCoverRule, 61>& rule_table() noexcept {
    static const std::array<GroundCoverRule, 61> table = [] {
        using namespace ambient;
        std::array<GroundCoverRule, 61> t{};
        unsigned n = 0;
        // The table size is exact: one more rule must grow it (a write past it corrupts memory).
        const auto add = [&](GroundCoverRule r) { if (n >= t.size()) std::abort(); t[n++] = r; return &t[n - 1]; };
        const auto clump = [](GroundCoverRule* r, float f, float lo, float hi) { r->clump_freq = f; r->clump_lo = lo; r->clump_hi = hi; };
        const auto tint = [](GroundCoverRule* r, float a, float b, float c) { r->tint[0] = a; r->tint[1] = b; r->tint[2] = c; };
        const auto own = [](GroundCoverRule* r, float a, float b, float c) { r->own[0] = a; r->own[1] = b; r->own[2] = c; };
        const auto field = [](GroundCoverRule* r, std::uint32_t f, float a, float b) { r->field = f; r->field_a = a; r->field_b = b; };
        const std::uint32_t grass = area_bit(area_grass), bare = area_bit(area_bare), dry = area_bit(area_dry),
                            sand = area_bit(area_sand), snow = area_bit(area_snow), water = area_bit(area_water);
        const std::uint32_t dead_city = zone_bit(zone_dead_city);
        // Litter needs broadleaf trees: none in Suslanger (palms, cacti), whose
        // flora the shared tree count already leaves out.
        const std::uint32_t woods = zone_bit(zone_outdoor) | zone_bit(zone_snow) | dead_city;
        // Grass tiles support meadow grass in every outdoor zone, Dead City
        // included; caves keep their own moss/fern set on grass-typed tiles.
        const std::uint32_t meadow = kOutdoorZones | dead_city;
        auto* g = add(make_rule(gm_blades, grass, 16, .24f, .44f)); g->zones=meadow; tint(g, 1, 1.07f, .9f); clump(g, .3f, .18f, .42f);
        auto* big = add(*g); big->density = 1; big->scale_min = .36f; big->scale_max = .54f; big->lod_near = 25; big->lod_far = 70;
        // Flowers gather in patches 2-3 m wide (one or two species each), about
        // one per 64 m2 of open meadow and fewer under trees and beside roads,
        // with bare grass between: ~35% of the former evenly clumped flowers,
        // about twice as dense inside a patch.
        auto* fl = add(make_rule(gm_flower, grass, .7f, .26f, .38f)); tint(fl, 1, 1.07f, .9f); fl->jitter = 0;
        fl->zones=meadow;
        fl->palette = pal_flower; fl->cluster = cl_flower_patch;
        // Sparse grass on bare ground, dry grass on dry tiles and sand, frosted tufts on snow.
        // Placement further limits these tufts to the grass-facing edges;
        // interiors keep only 1% of this already sparse candidate density.
        auto* gs = add(*g); gs->areas = bare; gs->zones=kOutdoorZones; gs->density = .4f; clump(gs, .45f, .48f, .66f);
        auto* dg = add(*g); dg->areas = dry; dg->zones=kOutdoorZones; dg->density = 14; tint(dg, .92f, .86f, .6f); clump(dg, .5f, .55f, .75f);
        auto* ds = add(*dg); ds->areas = sand; ds->density = 1;
        // Snow: dry pale straw bunched in tight clumps of ~6 tufts, one clump
        // per ~16 m2 with white between, buried so only the top 35-55% of the
        // tallest blades shows (painted snow at the snow line).
        auto* fs = add(*dg); fs->areas = snow; fs->density = 1.5f; tint(fs, 1.05f, 1.02f, .95f); fs->frost = .2f;
        fs->own_mix = .7f; own(fs, .85f, .78f, .95f); clump(fs, .3f, 0, 0); fs->cluster = cl_snow_clump; fs->bury = .65f;
        // Dead City flavours dry/bare ground; normal grass tiles stay grassy.
        auto* dc = add(*dg); dc->areas = dry | bare; dc->zones = dead_city; dc->density = 3; tint(dc, .8f, .76f, .6f);
        // Stones (scaled down after review) and pebbles, also in caves.
        auto* peb = add(make_rule(gm_pebble, grass | sand, .12f, .012f, .03f)); peb->zones = kAllZones;
        auto* pebb = add(make_rule(gm_pebble, bare | dry, .8f, .012f, .035f)); pebb->zones = kAllZones;
        auto* st = add(make_rule(gm_stone, grass | sand | dry, .01f, .04f, .09f)); st->zones = kAllZones;
        auto* stb = add(make_rule(gm_stone, bare, .08f, .04f, .11f)); stb->zones = kAllZones;
        auto* sts = add(*stb); sts->areas = snow; sts->density = .05f; sts->frost = .85f; sts->zones = kOutdoorZones;
        // near_trees: 1 leafy deciduous, -1 conifer, 2 any woody tree,
        // 3 non-conifer wood (Dead City's bare trees also shed yellow leaves).
        // Each influence is independent: a neighbouring broadleaf still sheds
        // leaves beside a conifer. Bare trees never contribute leaf influence.
        const std::uint32_t cold = zone_bit(zone_snow), warm_woods = woods & ~(cold | dead_city);
        // Half the reviewed forest litter, with gaps in the broadleaf canopy
        // influence instead of a uniform carpet. The mild clump filter keeps
        // most candidates, so halving density remains the main reduction.
        auto* leaf = add(make_rule(gm_leaf, grass | bare, 4.5f, .12f, .2f)); leaf->near_trees = 1; leaf->zones = warm_woods;
        clump(leaf, .45f, .15f, .35f);
        auto* leafd = add(*leaf); leafd->areas = dry | sand; leafd->density = 1.5f;
        // Dead City retains abundant yellow litter around its bare trees too.
        auto* leafdc = add(*leaf); leafdc->zones = dead_city; leafdc->near_trees = 3; leafdc->density = 9;
        clump(leafdc, .3f, 0, 0); // retain its reviewed density outside the forest reduction
        leafdc->palette = pal_none; leafdc->own_mix = 1; own(leafdc, .95f, .78f, .28f);
        auto* leafdcd = add(*leafdc); leafdcd->areas = dry | sand; leafdcd->density = 3;
        // Snow region, non-snow ground: sparse, dull brown (no autumn palette).
        auto* leafc = add(*leaf); leafc->areas = grass | bare | dry; leafc->zones = cold; leafc->density = 1.2f;
        clump(leafc, .3f, 0, 0); // preserve the existing sparse Ingos placement
        leafc->palette = pal_none; leafc->own_mix = 1; own(leafc, .36f, .29f, .21f);
        // A modest increase in the little piles; the existing conifer-only
        // crown falloff keeps gaps between trees and confines the litter.
        auto* needles = add(make_rule(gm_needles, grass | bare | dry, 7, .25f, .45f));
        needles->zones = woods; needles->near_trees = -1;
        // Under conifers on snow: sparse, dark and snow-dusted.
        auto* needless = add(*needles); needless->areas = snow; needless->density = 1; needless->frost = .5f;
        needless->own_mix = .6f; own(needless, .3f, .32f, .26f);
        auto* cone = add(make_rule(gm_cone, grass | bare | dry, .08f, .06f, .12f));
        cone->zones = woods; cone->near_trees = -1;
        // All fallen branches and deadwood require nearby trees.
        auto* tw = add(make_rule(gm_twig, grass | bare | dry, .12f, .22f, .45f)); tw->near_trees = 2; tw->zones = woods;
        auto* twf = add(make_rule(gm_twig_fork, grass | bare | dry, .1f, .25f, .5f)); twf->near_trees = 2; twf->zones = tw->zones;
        auto* twb = add(make_rule(gm_twig_broken, grass | bare | dry, .08f, .3f, .55f)); twb->near_trees = 2; twb->zones = tw->zones;
        auto* twa = add(make_rule(gm_twig_broken, bare | dry, .03f, .3f, .55f)); twa->zones = tw->zones; twa->near_trees = 2;
        auto* tws = add(make_rule(gm_twig, snow, .1f, .22f, .45f)); tws->near_trees = -1; tws->frost = .6f; tws->zones = woods;
        own(tws, .6f, .58f, .55f);
        auto* lg = add(make_rule(gm_log, grass | bare, .012f, .8f, 1.3f)); lg->near_trees = 2; lg->zones = tw->zones;
        auto* lga = add(make_rule(gm_log, bare, .003f, .8f, 1.3f)); lga->zones = tw->zones; lga->near_trees = 2;
        // Removed dead-bush rules. Reserve their seed slots so all other
        // cover keeps its placement; neither slot has any habitat or candidates.
        add(GroundCoverRule{});
        add(GroundCoverRule{});
        // Snow piles in Ingos only, rounded and soft (receive shadows only).
        auto* mound = add(make_rule(gm_mound, snow, .06f, .6f, 1.4f)); mound->zones = zone_bit(zone_snow); tint(mound, 1.06f, 1.06f, 1.05f);
        mound->blend_height = .2f;
        // Soft sand piles: the same deformable pile, lower and smaller, only
        // where every corner around it is a soft sand family (material
        // identity, terrain_tile_soft_sand); its colour is the sand's own.
        auto* dune = add(make_rule(gm_mound, sand, .05f, .4f, .9f)); dune->zones = kOutdoorZones;
        dune->flags = rf_snow_pile | rf_sand_pile; dune->sz_min = .5f; dune->sz_max = .8f; dune->blend_height = .2f;
        // A band around swamps (never on the swamp layer itself): reeds, cattails, rotten wood.
        auto* reed = add(make_rule(gm_reed, kLand, 7, .9f, 1.5f)); field(reed, fd_swamp_edge, 0, 3); clump(reed, .35f, .1f, .3f);
        tint(reed, .8f, .9f, .75f);
        auto* cat = add(make_rule(gm_cattail, kLand, 1.2f, 1, 1.4f)); field(cat, fd_swamp_edge, 0, 2.2f); clump(cat, .35f, .35f, .55f);
        auto* rot = add(make_rule(gm_twig_broken, kLand, .12f, .3f, .55f)); rot->zones = woods; rot->near_trees = 2; field(rot, fd_swamp_edge, 0, 2.5f); own(rot, .62f, .62f, .52f);
        auto* rotf = add(make_rule(gm_twig_fork, kLand, .1f, .25f, .5f)); rotf->zones = woods; rotf->near_trees = 2; field(rotf, fd_swamp_edge, 0, 2.5f); own(rotf, .62f, .62f, .52f);
        auto* rotl = add(make_rule(gm_log, kLand, .012f, .8f, 1.4f)); rotl->zones = woods; rotl->near_trees = 2; field(rotl, fd_swamp_edge, 0, 3); own(rotl, .62f, .62f, .52f);
        // Sparse reeds on lake and river banks.
        auto* rr = add(*reed); field(rr, fd_river, 0, 1.6f); rr->density = 2.5f;
        // Sea shores only (never lakes or rivers): wrack line of kelp, shells and conches.
        // Kelp lies in irregular clumps (kelp_shore_clump) with long bare
        // stretches, each clump nearer or farther from the water, ~40% of the
        // former even line; denser inside a clump. All beach wrack keeps to
        // sand (ETERRTYPE sand, or a tile whose atlas slot has a sand corner
        // family: gcSandFamilyAt) on gentle ground near the water (kelp_ground).
        auto* kelp = add(make_rule(gm_kelp, sand, 8, .4f, .9f)); field(kelp, fd_beach, .15f, 1.4f);
        kelp->flags |= rf_shore_clumps;
        auto* shell = add(make_rule(gm_shell, sand, .6f, .08f, .12f)); field(shell, fd_beach, 0, 5);
        auto* conch = add(make_rule(gm_conch, sand, .08f, .07f, .1f)); field(conch, fd_beach, 0, 4);
        // Under the sea only: depth-dependent seagrass and taller kelp-like grass.
        auto* sea = add(make_rule(gm_seagrass, water, 6, .4f, .8f)); field(sea, fd_depth, .4f, 3.5f); clump(sea, .3f, .3f, .55f);
        auto* tall = add(make_rule(gm_seagrass, water, 1.2f, 1, 1.8f)); field(tall, fd_depth, 1.5f, 6.5f); clump(tall, .25f, .4f, .6f);
        auto* sshell = add(*shell); sshell->areas = water; field(sshell, fd_depth, 0, 2); sshell->density = .3f;
        // Cave sets use the drawn base type and liquid layer, never a map-name
        // exception. The lava band follows the actual shoreline, not ground
        // types below an overlay. Ground-art materials sample the painted tile.
        const auto caves = zone_bit(zone_cave) | zone_bit(zone_gipat_cave);
        const auto cave = [&](std::uint32_t mesh, std::uint32_t areas, float d, float lo, float hi) {
            auto* r = add(make_rule(mesh,areas,d,lo,hi)); r->zones=caves; return r;
        };
        auto* ash = cave(gm_patch,bare|dry,.65f,.12f,.32f); field(ash,fd_lava_edge,0,4); tint(ash,.68f,.68f,.66f);
        auto* scoria = cave(gm_pebble,bare|dry,.5f,.02f,.06f); field(scoria,fd_lava_edge,0,5); tint(scoria,.58f,.55f,.5f);
        auto* obsidian = cave(gm_shard,bare|dry,.12f,.06f,.14f); field(obsidian,fd_lava_edge,0,3); tint(obsidian,.28f,.3f,.34f);
        auto* crust = cave(gm_slab,bare|dry,.35f,.18f,.4f); field(crust,fd_lava_edge,0,1.8f); tint(crust,.48f,.43f,.39f);
        auto* moss = cave(gm_patch,bare|grass|sand,1.2f,.18f,.5f); field(moss,fd_damp,0,5); tint(moss,.65f,.93f,.62f); clump(moss,.35f,.4f,.65f);
        auto* lichen = cave(gm_patch,bare,.18f,.1f,.32f); lichen->flags=rf_rock; field(lichen,fd_wall,0,2); tint(lichen,.86f,.94f,.72f);
        auto* fern = cave(gm_fern,bare|grass|sand,.24f,.18f,.4f); field(fern,fd_damp,0,4); tint(fern,.62f,.94f,.57f);
        auto* roots = cave(gm_roots,bare|grass|sand,.04f,.4f,.9f); field(roots,fd_wall,.15f,1.2f);
        auto* mushroom = cave(gm_mushroom,bare|grass|sand,.14f,.08f,.18f); field(mushroom,fd_damp,0,6);
        auto* puddle = cave(gm_patch,bare,.045f,.22f,.6f); puddle->material=mat_wet; field(puddle,fd_damp,0,3);
        auto* slab = cave(gm_slab,bare,.08f,.1f,.25f); slab->flags=rf_rock;
        auto* rubble = cave(gm_shard,bare,.2f,.04f,.12f); rubble->flags=rf_rock;
        auto* bones = cave(gm_bones,bare|dry,.025f,.15f,.35f); bones->zones|=dead_city; field(bones,fd_undead,0,1);
        auto* skull = cave(gm_skull,bare|dry,.009f,.1f,.16f); skull->zones|=dead_city; field(skull,fd_undead,0,1);
        auto* web = cave(gm_web,bare,.018f,.25f,.65f); field(web,fd_corner,0,2);
        auto* crystals = cave(gm_crystal,bare,.065f,.03f,.09f); crystals->flags=rf_rock;
        auto* cave_snow = add(*mound); cave_snow->zones=caves; cave_snow->density=.04f;
        return t;
    }();
    return table;
}

// Shore kelp clumps (rf_shore_clumps). Mirrors gcPlaceCS: the patch lattice
// of gcPatchAt (ground_cover_interaction.h) with gcKelpShape, a candidate kept
// only inside its cell's lumpy patch outline, and the shore distance its band
// is tested at shifted per clump (+-1 m) and per instance (+-0.5 m; random01
// is the instance's next gcRand). False: outside every clump.
constexpr float kKelpCell = 4, kKelpRadiusMin = .6f, kKelpRadiusMax = 1.6f, kKelpPresence = .7f;
constexpr std::uint32_t kKelpSeed = 0x4b1d0e57u;
inline std::uint32_t patch_hash(std::uint32_t x) noexcept {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
inline bool kelp_shore_clump(float x, float y, float shore, float random01, float& edge) noexcept {
    const int cx = int(std::floor(x / kKelpCell)), cy = int(std::floor(y / kKelpCell));
    const std::uint32_t h = patch_hash(std::uint32_t(cx) * 73856093u ^ std::uint32_t(cy) * 19349663u ^ kKelpSeed);
    const std::uint32_t id = patch_hash(h ^ 0x68e31da4u);
    const float radius = kKelpRadiusMin + (kKelpRadiusMax - kKelpRadiusMin) * float(h & 255u) / 255.f;
    const float margin = radius * 1.25f, room = std::max(kKelpCell - 2 * margin, 0.f);
    const float dx = x - (cx * kKelpCell + margin + float((h >> 8) & 255u) / 255.f * room);
    const float dy = y - (cy * kKelpCell + margin + float((h >> 16) & 255u) / 255.f * room);
    const float a = std::atan2(dy, dx);
    const float lump = 1 + .25f * (.6f * std::sin(2 * a + float(id & 255u) * .0246f) + .4f * std::sin(3 * a + float((id >> 8) & 255u) * .0246f));
    if (float(h >> 24) / 256.f >= kKelpPresence || std::sqrt(dx * dx + dy * dy) > radius * lump) return false;
    edge = shore - (float((id >> 16) & 255u) / 255.f * 2 - 1) - (random01 - .5f);
    return true;
}
// Beach wrack ground (fd_beach: kelp, shells, conches): slope under 20 degrees (ground normal z) and
// at most kKelpAboveWater metres above the nearest water surface, so never on
// cliffs or banks high above the sea. Mirrors gcPlaceCS (kGcKelpSlopeCos,
// kGcKelpAboveWater; gcAboveWater finds the water surface).
constexpr float kKelpSlopeCos = .9397f, kKelpAboveWater = .8f;
inline bool kelp_ground(float normal_z, float above_water) noexcept {
    return normal_z >= kKelpSlopeCos && above_water <= kKelpAboveWater;
}

// Quality levels of the "ground_cover" switch: distance scale of the full-
// density ranges (the far LOD density always lasts to the farthest visible
// ground) and density. Level 0 creates nothing; 2 is the default.
struct Quality { float distance = 0, density = 0; };
inline Quality quality_level(int level) noexcept {
    if (level <= 0) return {};
    if (level == 1) return {.6f, .55f};
    if (level == 2) return {1, 1};
    return {1.6f, 1};
}

// Unit interaction: how far and how strongly a live body bends the grass
// around it. The body footprint is the liquid ripples' (envelope radius *
// .35). Small and human-sized bodies part the blades around their legs at
// half strength; large and bigger creatures press them fully.
struct Push { float radius = 0, strength = 0; };
inline Push unit_push(float envelope_radius) noexcept {
    const float footprint = envelope_radius > 0 ? std::clamp(envelope_radius * .35f, .3f, 2.5f) : .5f;
    float t = std::clamp((footprint - .75f) / .5f, 0.f, 1.f);
    t = t * t * (3 - 2 * t);
    return {footprint + .4f, .5f + .5f * t};
}

// Wind drive of the cover sway (all tufts, reeds and bushes share it). A
// stronger wind bends further along a saturating curve (a storm at speed
// ~2.35 bends ~1.5x a calm .85 wind; with its stronger gust lean ~1.8x), but
// never shakes faster: the gust fronts' phase rate is constant. The shader
// phase is seconds * travel, so a rate that followed the varying wind speed
// would also race the phase by elapsed seconds times the rate's change.
// Speeds up to 1 (calm and mild wind) keep the previous linear drive.
struct SwayDrive { float amplitude = 0, travel = 0; };
constexpr float kSwayTravel = .75f * .85f; // the previous calm rate, radians per game second
inline SwayDrive sway_drive(float force, float speed, bool sheltered) noexcept {
    constexpr float knee = 1.f, cap = 1.25f;
    const float s = std::max(speed, 0.f);
    const float strength = s <= knee ? s : knee + (cap - knee) * (1 - std::exp(-(s - knee) / (cap - knee)));
    return {(.06f + .5f * force) * strength * (sheltered ? .2f : 1.f), kSwayTravel};
}

// Compact pressing source: a game-visible track, shown corpse or shown live
// body. Mirrors GcFootprint in the splat shader; the GPU owns recovery.
struct FootprintSplat {
    std::array<float,4> rectangle{}; // centre xy, half size xy, metres
    std::array<float,4> effect{};    // rotation cos/sin, strength, w: >0 live body, <0 lasting body, 0 corpse
};
static_assert(sizeof(FootprintSplat) == 32);

// Ground contact of one posed static part, from its own triangles: the
// kContactCell world cells covered by every triangle portion inside the
// contact slab, from kContactDepth below to kContactBand above the base
// ground under each vertex. A vertical trunk or wall leaves its base line,
// faces lying on or just in the ground their area; suspended and wholly
// buried geometry nothing. Never the part's bounding box or envelope, which
// spans a whole tree's height or a diagonal log's square. Each vertex is
// {x, y, height above the ground}; a lower bound suffices for vertices
// clearly above the slab. Cells merge into row runs and runs into
// rectangles; the GPU adds a soft 20 cm margin.
constexpr float kContactCell = .25f, kContactBand = .15f, kContactDepth = .5f;
inline void contact_footprints(const std::vector<std::array<float,3>>& vertices,
                               const std::vector<std::uint32_t>& triangles,
                               std::vector<std::uint64_t>& cells,
                               std::vector<std::array<float,4>>& rectangles) {
    cells.clear();
    const auto mark = [&](float x, float y) {
        const auto cx = std::uint32_t(std::int32_t(std::floor(x / kContactCell)) + 0x40000000);
        const auto cy = std::uint32_t(std::int32_t(std::floor(y / kContactCell)) + 0x40000000);
        cells.push_back((std::uint64_t(cy) << 32) | cx);
    };
    for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
        std::array<std::array<float,3>,3> in{};
        bool valid = true;
        for (unsigned k = 0; k < 3; ++k) {
            if (triangles[t + k] >= vertices.size()) { valid = false; break; }
            in[k] = vertices[triangles[t + k]];
            if (!std::isfinite(in[k][0]) || !std::isfinite(in[k][1]) || !std::isfinite(in[k][2])) valid = false;
        }
        if (!valid || (in[0][2] > kContactBand && in[1][2] > kContactBand && in[2][2] > kContactBand) ||
            (in[0][2] < -kContactDepth && in[1][2] < -kContactDepth && in[2][2] < -kContactDepth)) continue;
        // The triangle's part inside the slab (convex, at most 5 points).
        std::array<std::array<float,3>,5> clipped[2]{};
        unsigned count[2]{3, 0};
        for (unsigned k = 0; k < 3; ++k) clipped[0][k] = in[k];
        for (unsigned plane = 0; plane < 2; ++plane) {
            const auto& from = clipped[plane & 1]; auto& to = clipped[(plane + 1) & 1];
            const auto outside = [&](const std::array<float,3>& p) {
                return plane == 0 ? p[2] - kContactBand : -kContactDepth - p[2]; };
            unsigned out = 0;
            for (unsigned k = 0; k < count[plane & 1]; ++k) {
                const auto& p = from[k]; const auto& q = from[(k + 1) % count[plane & 1]];
                const float dp = outside(p), dq = outside(q);
                if (dp <= 0) to[out++] = p;
                if ((dp <= 0) != (dq <= 0)) {
                    const float f = dp / (dp - dq);
                    to[out++] = {p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f, p[2] + (q[2] - p[2]) * f};
                }
            }
            count[(plane + 1) & 1] = out;
        }
        const unsigned n = count[0];
        if (!n) continue;
        std::array<std::array<float,2>,5> poly{};
        for (unsigned k = 0; k < n; ++k) poly[k] = {clipped[0][k][0], clipped[0][k][1]};
        // Its outline (a wall's contact line has no area)...
        for (unsigned k = 0; k < n; ++k) {
            const auto& a = poly[k]; const auto& b = poly[(k + 1) % n];
            const float length = std::hypot(b[0] - a[0], b[1] - a[1]);
            const int steps = int(std::ceil(length / (kContactCell * .5f)));
            for (int s = 0; s <= steps; ++s) {
                const float f = steps ? float(s) / float(steps) : 0.f;
                mark(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f);
            }
        }
        // ...and the cells whose centres it covers.
        float area = 0, lo[2]{poly[0][0], poly[0][1]}, hi[2]{poly[0][0], poly[0][1]};
        for (unsigned k = 0; k < n; ++k) {
            const auto& a = poly[k]; const auto& b = poly[(k + 1) % n];
            area += a[0] * b[1] - b[0] * a[1];
            for (unsigned axis = 0; axis < 2; ++axis) { lo[axis] = std::min(lo[axis], a[axis]); hi[axis] = std::max(hi[axis], a[axis]); }
        }
        if (n < 3 || std::abs(area) < 1e-6f) continue;
        const float sign = area > 0 ? 1.f : -1.f;
        for (float cy = (std::floor(lo[1] / kContactCell) + .5f) * kContactCell; cy <= hi[1]; cy += kContactCell)
            for (float cx = (std::floor(lo[0] / kContactCell) + .5f) * kContactCell; cx <= hi[0]; cx += kContactCell) {
                bool inside = true;
                for (unsigned k = 0; k < n && inside; ++k) {
                    const auto& a = poly[k]; const auto& b = poly[(k + 1) % n];
                    inside = sign * ((b[0] - a[0]) * (cy - a[1]) - (b[1] - a[1]) * (cx - a[0])) >= 0;
                }
                if (inside) mark(cx, cy);
            }
    }
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    // Row runs (x0, x1, y), then identical runs on consecutive rows merged.
    struct Run { std::int32_t x0, x1, y0, y1; };
    std::vector<Run> runs;
    for (std::size_t i = 0; i < cells.size();) {
        const auto y = std::int32_t(cells[i] >> 32) - 0x40000000;
        const auto x0 = std::int32_t(cells[i] & 0xffffffffu) - 0x40000000;
        std::int32_t x1 = x0;
        ++i;
        while (i < cells.size() && std::int32_t(cells[i] >> 32) - 0x40000000 == y &&
               std::int32_t(cells[i] & 0xffffffffu) - 0x40000000 == x1 + 1) { ++x1; ++i; }
        runs.push_back({x0, x1, y, y});
    }
    std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) {
        return a.x0 != b.x0 ? a.x0 < b.x0 : a.x1 != b.x1 ? a.x1 < b.x1 : a.y0 < b.y0; });
    rectangles.clear();
    for (std::size_t i = 0; i < runs.size();) {
        Run r = runs[i++];
        while (i < runs.size() && runs[i].x0 == r.x0 && runs[i].x1 == r.x1 && runs[i].y0 == r.y1 + 1) r.y1 = runs[i++].y0;
        rectangles.push_back({(float(r.x0) + float(r.x1) + 1) * kContactCell * .5f, (float(r.y0) + float(r.y1) + 1) * kContactCell * .5f,
                              float(r.x1 - r.x0 + 1) * kContactCell * .5f, float(r.y1 - r.y0 + 1) * kContactCell * .5f});
    }
}
inline FootprintSplat object_splat(const std::array<float,4>& rectangle) noexcept { return {rectangle, {1, 0, 1, 0}}; }

// The game fades once per segment. Interpolate its remaining lifetime over
// the same fade duration using the authoritative fractional segment clock.
// Visibility is normalized to the print's full (terrain-dependent) alpha;
// smoothstep retains both endpoints and eases the final return to upright.
inline float footprint_visibility(double remaining_segments, int fade_segments) noexcept {
    if(fade_segments<=0)return 0.f;
    const float v=static_cast<float>(std::clamp(remaining_segments/fade_segments,0.0,1.0));
    return v*v*(3.f-2.f*v);
}

// A marker retains the creator's body stamp, rather than the texture's foot
// rectangle. Negative w identifies a lasting body stamp; its radial profile
// and direction field are identical to the live body at full visibility.
inline FootprintSplat footprint_splat(bool visible, FootprintSplat created,
                                     float visibility) noexcept {
    created.effect[2] *= visible ? std::clamp(visibility, 0.f, 1.f) : 0.f;
    created.effect[3] = -1.f;
    return created;
}

// The window-relative object mask still holds `current` when it was drawn
// from the identical footprints at the same window origin into this texture.
inline bool object_mask_current(bool drawn, int drawn_x, int drawn_y, const std::vector<FootprintSplat>& drawn_list,
                                int x, int y, const std::vector<FootprintSplat>& current) noexcept {
    return drawn && drawn_x == x && drawn_y == y && drawn_list.size() == current.size() &&
        (current.empty() || std::memcmp(drawn_list.data(), current.data(), current.size() * sizeof(FootprintSplat)) == 0);
}

// A conservative cover window: intersect far-corner rays with the decoded
// terrain's lowest height, never a guessed plane under the camera or a pixel
// cutoff. The finite map bounds stop a horizon view at the actual map extent.
inline float visible_ground_reach(const float inverse[16], const float eye[3],
                                  float lowest_ground, float highest_ground,
                                  float map_x, float map_y) noexcept {
    float reach = 0, map_reach = 0;
    for (int corner = 0; corner < 4; ++corner) {
        const float ndc[2]{corner & 1 ? 1.f : -1.f, corner & 2 ? 1.f : -1.f};
        float far_point[4]{};
        for (unsigned r = 0; r < 4; ++r)
            far_point[r] = inverse[r * 4] * ndc[0] + inverse[r * 4 + 1] * ndc[1] + inverse[r * 4 + 2] + inverse[r * 4 + 3];
        if (std::abs(far_point[3]) > 1e-12f) {
            const float d[3]{far_point[0] / far_point[3] - eye[0], far_point[1] / far_point[3] - eye[1], far_point[2] / far_point[3] - eye[2]};
            const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            float distance = length;
            if (d[2] < 0 && lowest_ground < eye[2]) distance *= std::min(1.f, (lowest_ground - eye[2]) / d[2]);
            if (std::isfinite(distance)) reach = std::max(reach, distance);
        } else {
            // An infinite far plane: only the finite terrain bounds limit it.
            reach = INFINITY;
        }
        const float dx = (corner & 1 ? map_x : 0) - eye[0], dy = (corner & 2 ? map_y : 0) - eye[1];
        const float dz = std::max(std::abs(lowest_ground - eye[2]), std::abs(highest_ground - eye[2]));
        map_reach = std::max(map_reach, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return std::min(reach, map_reach);
}

// Neighbour maps: the neighbour terrain is drawn at active_to_world * (local +
// layout offset). Cover places in the neighbour's own map metres (its grids)
// and adds this world origin, so only a translation keeps both identical.
// Anything else returns false: that neighbour's cover is omitted, never bent.
constexpr float kNeighbourCoverDensity = 1.f; // same placement density across seams; distance LOD thins both
inline bool neighbour_cover_origin(const std::array<float, 16>& active_to_world, double offset_x, double offset_y,
                                   std::array<float, 3>& origin) noexcept {
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            if (std::abs(active_to_world[r * 4 + c] - (r == c ? 1.f : 0.f)) > 1e-5f) return false;
    if (active_to_world[12] != 0 || active_to_world[13] != 0 || active_to_world[14] != 0 || active_to_world[15] != 1) return false;
    for (unsigned r = 0; r < 3; ++r)
        origin[r] = active_to_world[r * 4 + 3] + float(r == 0 ? offset_x : r == 1 ? offset_y : 0.0);
    return std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2]);
}
// The neighbour's local cells (cell metres) within reach of the world eye,
// clamped to its map. False when none are. reach may be infinite.
struct CellWindow { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
inline bool neighbour_cover_window(const float eye[3], const std::array<float, 3>& origin, float reach,
                                   float map_x, float map_y, float cell, CellWindow& window) noexcept {
    if (!(reach > 0) || !(cell > 0)) return false;
    const float ex = eye[0] - origin[0], ey = eye[1] - origin[1];
    // Nearest point of the neighbour rectangle beyond reach: nothing to place.
    const float dx = std::max({0.f, -ex, ex - map_x}), dy = std::max({0.f, -ey, ey - map_y});
    if (dx * dx + dy * dy >= reach * reach) return false;
    const float cells_x = std::ceil(map_x / cell), cells_y = std::ceil(map_y / cell);
    window.x0 = int(std::max(0.f, std::floor((ex - reach) / cell)));
    window.y0 = int(std::max(0.f, std::floor((ey - reach) / cell)));
    window.x1 = int(std::min(cells_x, std::ceil((ex + reach) / cell)));
    window.y1 = int(std::min(cells_y, std::ceil((ey + reach) / cell)));
    return window.x1 > window.x0 && window.y1 > window.y0;
}
// Narrows a neighbour's cell window to the cells not wholly in full neighbour
// seam fog (the boxes neighbour_fog_hides rejects); false when none remain.
// fog is world-space; sides without a rendered neighbour never clip.
inline bool neighbour_cover_fog_window(const std::array<float, 3>& origin, float cell,
                                       const NeighbourFogField& fog, CellWindow& window) noexcept {
    const auto clip = [cell](double lo, double hi, double o, int& a, int& b) {
        // Cell k covers [k*cell,(k+1)*cell] locally: hidden at or below lo, at or above hi.
        const double first = std::floor((lo - o) / cell), last = std::ceil((hi - o) / cell);
        if (first > a) a = first >= b ? b : int(first);
        if (last < b) b = last <= a ? a : int(last);
    };
    const double reach = neighbour_fog_cull_distance(fog);
    clip(fog.seam.x0 - reach, fog.seam.x1 + reach, origin[0], window.x0, window.x1);
    clip(fog.seam.y0 - reach, fog.seam.y1 + reach, origin[1], window.y0, window.y1);
    return window.x1 > window.x0 && window.y1 > window.y0;
}

// The unit's race locomotion (units.udb race field 5, copied into its
// replicated stats at CUnit+0x33F): 1 walk, 2 levitate, 3 stay. Every
// shipped Base/Dejavu race with 2 hovers or flies (bats, dragons, banshees,
// beholders, succubi, shadows, will-o'-wisps, ancient curses, Dejavu
// octopuses); these never bend or press cover, however low they hover.
constexpr std::uint8_t kLevitatingLocomotion = 2;
inline bool unit_presses_cover(std::uint8_t locomotion) noexcept { return locomotion != kLevitatingLocomotion; }

// Called only for the bridge's visible, grounded live-unit snapshot. The
// radial falloff is exactly the live blade push, retained after it moves away.
inline FootprintSplat live_unit_splat(float x, float y, float envelope_radius) noexcept {
    const Push push = unit_push(envelope_radius);
    return {{x, y, push.radius, push.radius}, {1.f, 0.f, push.strength, 1.f}};
}

// Live bodies use the unit's stable collision footprint, never the animated
// model envelope. Every grounded body presses fully, with the same radial
// falloff and recovery for every stance and gait.
inline FootprintSplat live_body_splat(float x, float y, float half_extent) noexcept {
    const float radius = std::clamp(half_extent, .3f, 2.5f) + .4f;
    return {{x, y, radius, radius}, {1.f, 0.f, 1.f, 1.f}};
}

// GPU recovery of the shared body field in game seconds. Lasting markers
// and live bodies stamp their radial target immediately; source-free texels
// release continuously. Corpses retain the existing rate-limited rise.
// Mirrors gcTrailDecayPS + gcTrailPS with MAX blending.
constexpr float kTrailPressSeconds = .4f;
constexpr float kTrailReleaseSeconds = 1.5f;
constexpr double kSecondsPerSegment = .055;
// An expired source is needed only while GPU recovery could still contain
// its last stamp. This also bounds metadata when cover is switched off.
inline bool footprint_history_needed(double expiry_segment, double now_segment) noexcept {
    return now_segment-expiry_segment < kTrailReleaseSeconds/kSecondsPerSegment;
}
inline float trail_step(float previous, float target, float seconds) noexcept {
    const float dt = std::max(seconds, 0.f);
    return std::clamp(std::max({previous - dt / kTrailReleaseSeconds,
                                std::min(previous + dt / kTrailPressSeconds, target)}), 0.f, 1.f);
}

// A corpse the game still shows lies as a body-sized footprint, long along
// the body's world-XY forward and sized from its envelope, pressing fully
// while the body lasts. Removal or fading leaves only trail_step recovery.
inline FootprintSplat corpse_splat(float x, float y, float forward_x, float forward_y,
                                   float envelope_radius) noexcept {
    const float body = envelope_radius > 0 ? envelope_radius : 1.f;
    const float length = std::sqrt(forward_x * forward_x + forward_y * forward_y);
    const float c = length > 1e-6f ? forward_x / length : 1.f, s = length > 1e-6f ? forward_y / length : 0.f;
    return {{x, y, std::clamp(body * .5f, .4f, 4.f), std::clamp(body * .25f, .25f, 2.f)}, {c, s, 1.f, 0.f}};
}

} // namespace owned::ground_cover
