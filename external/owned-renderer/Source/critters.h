#pragma once
// Renderer-owned ambient critters: pure species, zone, threat and carrion
// rules. No D3D, no engine state. The renderer (renderer_critters.cpp) feeds
// them the shared ambient area grid (ambient_particles.h), the zone class,
// the game clock and the ground-cover unit rows (bridge_ground_cover_units).
//
// Values follow the approved offline prototype (claude/critters-prototype,
// Prototypes/critters/behaviour.py and flocks.py, issue #68): species table,
// unit sight/hearing threat reach, Dead City wall ring, carrion delay.
#include "ambient_particles.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace owned::critters {

enum Species : std::uint8_t {
    sp_rat = 0, sp_spider, sp_larva, sp_frog, sp_lizard, sp_snake, sp_scolopendra, sp_dungbeetle,
    sp_bird, sp_raven, sp_vulture, sp_crab, sp_snowbird, sp_snowmouse, sp_bat, sp_count,
};
enum Exit : std::uint8_t { exit_cover, exit_water, exit_mound, exit_flock, exit_soar };
enum Bird : std::uint8_t {
    bird_finch = 0, bird_sparrow, bird_bunting, bird_snowbird, bird_crow, bird_raven, bird_vulture, bird_bat, bird_count,
    bird_none = 0xff,
};

// The species (spawn rule and player setting) a bird variant flies for.
constexpr Species bird_species(Bird b) noexcept {
    return b == bird_snowbird ? sp_snowbird : b == bird_crow || b == bird_raven ? sp_raven
         : b == bird_vulture ? sp_vulture : b == bird_bat ? sp_bat : sp_bird;
}

constexpr std::uint32_t zbit(ambient::ZoneClass z) noexcept { return 1u << z; }
constexpr std::uint32_t abit(ambient::AreaClass a) noexcept { return 1u << a; }

// The prototype's ETERRTYPE habitats on the shared area classes: "bare" is
// ground, stone, rock and road (also highrock/undefined), "dry" is drygrass
// and field.
constexpr std::uint32_t kBare = abit(ambient::area_bare), kGrass = abit(ambient::area_grass),
    kDry = abit(ambient::area_dry), kSand = abit(ambient::area_sand), kSnow = abit(ambient::area_snow),
    kSwamp = abit(ambient::area_swamp), kWater = abit(ambient::area_water);

struct SpeciesRule {
    float length;              // metres: the drawn mesh's largest ground extent
    std::uint32_t zones;       // ZoneClass bits
    std::uint32_t habitat;     // AreaClass bits it may stand on
    std::uint32_t near_mask;        // AreaClass bits required within near_r (shore, swamp edge)
    float near_r, weight, day, night;
    std::uint8_t group_lo, group_hi;
    float flee_r;              // hearing reach (m) of a unit walking with loudness 1
    float speed;               // dart speed m/s
    Exit exit;
    bool cute;                 // never in the Dead City
    Bird variants[3];          // bird variants of a flock species
};

// Larva crawl (prototype critters.py LARVA): one stride per completed wave.
constexpr float kLarvaLength = .07f, kLarvaStride = .12f * kLarvaLength;
constexpr float kLarvaPeriod = 1.8f, kLarvaFleePeriod = .8f;

inline const SpeciesRule& species_rule(Species s) noexcept {
    using namespace ambient;
    constexpr Bird n = bird_none;
    static const SpeciesRule rules[sp_count] = {
        // rat
        {.24f, zbit(zone_cave) | zbit(zone_gipat_cave), kBare, 0, 0, 1.f, 1.f, 1.f, 1, 1, 3.5f, 2.f, exit_cover, false, {n, n, n}},
        // spider
        {.12f, zbit(zone_cave) | zbit(zone_gipat_cave) | zbit(zone_dead_city), kBare, 0, 0, .7f, 1.f, 1.f, 1, 1, 2.f, 1.f, exit_cover, false, {n, n, n}},
        // larva: wherever a live crawler is (larva_habitat): the Gipat swamp
        // (zone4) and caves (zone5_1); speed follows the wave
        {kLarvaLength, zbit(zone_outdoor) | zbit(zone_cave) | zbit(zone_gipat_cave), kBare | kGrass | kDry | kSand, 0, 0, 2.f, 1.f, 1.f, 2, 4, 1.5f, kLarvaStride / kLarvaFleePeriod, exit_cover, false, {n, n, n}},
        // frog: near swamp or water
        {.065f, zbit(zone_outdoor), kGrass | kBare, kSwamp | kWater, 3.f, 1.f, 1.f, 2.f, 1, 1, 2.5f, 1.2f, exit_water, true, {n, n, n}},
        // lizard
        {.20f, zbit(zone_outdoor) | zbit(zone_desert), kBare | kSand | kDry, 0, 0, 1.f, 1.f, 0.f, 1, 1, 3.f, 2.6f, exit_cover, true, {n, n, n}},
        // snake
        {.41f, zbit(zone_outdoor) | zbit(zone_desert) | zbit(zone_dead_city), kGrass | kBare | kSand, 0, 0, .12f, 1.f, .5f, 1, 1, 3.f, 1.2f, exit_cover, false, {n, n, n}},
        // scolopendra: Suslanger only (desert class), never the Dead City
        {.217f, zbit(zone_desert), kBare | kSand | kDry, 0, 0, .35f, 1.f, 1.5f, 1, 1, 2.5f, 2.4f, exit_cover, false, {n, n, n}},
        // dung beetle: Suslanger only
        {.033f, zbit(zone_desert), kSand | kBare, 0, 0, .35f, 1.f, 0.f, 1, 1, 1.5f, .6f, exit_cover, true, {n, n, n}},
        // small birds, flocks: absent from Suslanger (desert zone identity)
        {.13f, zbit(zone_outdoor), kGrass | kDry | kBare, 0, 0, 1.2f, 1.f, 0.f, 3, 8, 5.f, 6.f, exit_flock, true, {bird_finch, bird_sparrow, bird_bunting}},
        // corvids: raven flocks on the Dead City outskirts; single wild ravens and
        // crows perched on trees in the wilderness (crows only in Ingos)
        {.6f, zbit(zone_dead_city) | zbit(zone_outdoor) | zbit(zone_snow), kGrass | kBare | kSnow, 0, 0, 1.f, 1.f, .3f, 3, 6, 7.5f, 6.5f,
         exit_flock, false, {bird_raven, bird_crow, n}},
        // vultures: Suslanger thermals
        {.75f, zbit(zone_desert), kBare | kGrass | kDry | kSand | kSnow | kSwamp, 0, 0, .25f, 1.f, 0.f, 1, 3, 9.f, 7.f, exit_soar, true, {bird_vulture, n, n}},
        // shore crab
        {.103f, zbit(zone_outdoor) | zbit(zone_desert), kSand | kBare, kWater, 3.f, 1.2f, 1.f, 1.f, 1, 2, 3.f, 1.2f, exit_water, true, {n, n, n}},
        // Ingos snow birds
        {.13f, zbit(zone_snow), kSnow | kBare, 0, 0, 1.2f, 1.f, 0.f, 3, 6, 5.f, 6.f, exit_flock, true, {bird_snowbird, n, n}},
        // Ingos snow mouse (no ermine)
        {.13f, zbit(zone_snow), kSnow, 0, 0, 1.f, 1.f, 1.f, 1, 1, 3.f, 2.f, exit_mound, true, {n, n, n}},
        // cave bat swarms (length: wingspan with the wings spread in its flight clip); far smaller and
        // darker than the game's attackable bats
        {.33f, zbit(zone_cave) | zbit(zone_gipat_cave), kBare | kGrass | kDry | kSand | kSwamp, 0, 0, 1.2f, 1.f, 1.f, 5, 8, 4.f, 3.5f,
         exit_flock, false, {bird_bat, n, n}},   // weight 1.2 while someone moves (species_weight)
    };
    return rules[s < sp_count ? s : 0];
}

// Display size, against the game's own character head: the human figures
// (figures.res unhuma/unhufe, part "hd", tall-build morph targets 4-7, the
// build of a 1.70-1.77 m figure) have heads 0.21-0.30 m crown to chin, mean
// about 0.25 m; the short build (morph 0, a 1.03 m figure) has 0.17-0.18 m.
// Every ambient critter's body (largest extent) stays between half and one
// such head: smaller reads as invisible, larger as a monster. Long thin
// bodies keep their visual mass near that rule while being longer (user,
// 2026-10-03): snakes 0.45-0.6 m, centipedes 0.30-0.35 m; larvae 0.2-0.28 m. Ravens, crows
// and vultures are rabbit/bat sized instead (unanhoha hare 0.68 m long,
// unanwiba bat 1.16 m span, 0.45 m long). Each animal takes one display
// scale at birth (camera motion never changes its world size): the readable
// target_px at the spawn camera, clamped to its species band.
constexpr float kHeadSize = .25f, kRabbitLength = .68f;
struct DisplaySize { float target_px, min_length, max_length; };   // body px; metres
inline DisplaySize species_size(Species s) noexcept {
    constexpr float h = kHeadSize;
    static const DisplaySize sizes[sp_count] = {
        {22, .8f * h, h}, {20, .55f * h, .85f * h}, {30, .2f, .28f}, {20, .5f * h, .7f * h},       // rat spider larva frog
        {22, .85f * h, h}, {40, .45f, .6f}, {30, .30f, .35f}, {18, .6f * h, .8f * h},             // lizard snake centipede beetle
        {18, .6f * h, .85f * h}, {44, .4f, .55f},                                                 // small birds, ravens
        {45, .5f, .65f},                                                                          // vulture (span ~2.5x)
        {20, .6f * h, .85f * h}, {18, .6f * h, .85f * h}, {20, .5f * h, .7f * h},                 // crab snowbird snowmouse
        {14, 1.2f * h, 1.8f * h}};                                                                // bat (wingspan, 2x)
    return sizes[s < sp_count ? s : 0];
}
// Authored body length (m) of each bird variant (proportions only: the bird
// meshes are built at unit length and the root applies length x scale).
inline float bird_length(Bird b) noexcept {
    static constexpr float lengths[bird_count] = {.13f, .14f, .15f, .14f, .45f, .6f, .75f, .33f};
    return lengths[b < bird_count ? b : 0];
}
inline DisplaySize bird_size(Bird b) noexcept {
    if (b == bird_vulture) return species_size(sp_vulture);
    if (b == bird_crow) return {40, .35f, .5f};
    if (b == bird_raven) return species_size(sp_raven);
    if (b == bird_bat) return species_size(sp_bat);
    return species_size(b == bird_snowbird ? sp_snowbird : sp_bird);
}
inline float display_scale(float length, const DisplaySize& size, float pixels_per_metre) noexcept {
    if (!(length > 0)) return 1;
    const float lo = size.min_length / length, hi = std::max(lo, size.max_length / length);
    if (!(pixels_per_metre > 0)) return lo;
    return std::clamp(size.target_px / (length * pixels_per_metre), lo, hi);
}
inline float species_scale(Species s, float pixels_per_metre) noexcept {
    return display_scale(species_rule(s).length, species_size(s), pixels_per_metre);
}
inline float bird_scale(Bird b, float pixels_per_metre) noexcept {
    return display_scale(bird_length(b), bird_size(b), pixels_per_metre);
}
// The simulated animals' world size: the middle of the species band, never
// derived from the view (the camera is a render-only input).
inline float band_scale(float length, const DisplaySize& size) noexcept {
    return length > 0 ? .5f * (size.min_length + size.max_length) / length : 1.f;
}
inline float species_scale(Species s) noexcept { return band_scale(species_rule(s).length, species_size(s)); }
inline float bird_scale(Bird b) noexcept { return band_scale(bird_length(b), bird_size(b)); }
// A spawn needs kSpawnPixels of body on screen (an animal on a far zoomed-out
// camera stays away); drawing culls below kMinPixels and birds under
// kFarPixels use the far LOD.
constexpr float kSpawnPixels = 6.f, kMinPixels = 2.f, kFarPixels = 8.f;
// Ground cover: lush grass, dry grass and fields carry 14-18 blades per m2 of
// 0.24-0.54 m (ground_cover.h rule table); bare ground, sand and snow carry
// 0.6-2. Ground animals prefer such open tiles so the cover does not hide them.
constexpr std::uint32_t kOpenGround = abit(ambient::area_bare) | abit(ambient::area_sand) | abit(ambient::area_snow);
constexpr float kOpenSearch = 4.f;   // metres a spawn may move to reach open ground

// Perspective differential on the ground plane, including the derivative of
// clip W. Uses the same row-major world-to-clip matrix as the shipped pass.
inline bool project_ground(const float* m, float width, float height,
    float x, float y, float z, float& nx, float& ny, float& ppm) noexcept {
    const float w = m[12]*x + m[13]*y + m[14]*z + m[15];
    if (!(w > 1e-4f)) return false;
    nx = (m[0]*x + m[1]*y + m[2]*z + m[3]) / w;
    ny = (m[4]*x + m[5]*y + m[6]*z + m[7]) / w;
    const float dx0 = .5f*width*(m[0]-nx*m[12])/w;
    const float dx1 = .5f*height*(m[4]-ny*m[12])/w;
    const float dy0 = .5f*width*(m[1]-nx*m[13])/w;
    const float dy1 = .5f*height*(m[5]-ny*m[13])/w;
    // Largest singular value: maximum projected length for a ground direction.
    const float a = dx0*dx0+dx1*dx1, b = dx0*dy0+dx1*dy1, c = dy0*dy0+dy1*dy1;
    ppm = std::sqrt(.5f*(a+c+std::hypot(a-c,2*b)));
    return std::isfinite(nx) && std::isfinite(ny) && std::isfinite(ppm);
}

// Unit stance from the current clip name (the game's own clip names:
// cwalk*, crun*, ccrawl*, uattack*, ucast*, uhit*, udeath* in figures.res).
enum Stance : std::uint8_t { stance_idle = 0, stance_walk, stance_run, stance_crawl, stance_attack };
inline bool clip_prefix(const char* name, const char* prefix) noexcept {
    for (; *prefix; ++name, ++prefix) {
        char c = *name;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c != *prefix) return false;
    }
    return true;
}
inline Stance stance_from_clip(const char* name) noexcept {
    if (!name) return stance_idle;
    if (clip_prefix(name, "uattack") || clip_prefix(name, "ucast"))
        return stance_attack;
    if (clip_prefix(name, "ccrawl")) return stance_crawl;
    if (clip_prefix(name, "crun")) return stance_run;
    if (clip_prefix(name, "cwalk")) return stance_walk;
    return stance_idle;
}

// How a unit is noticed, as the game's AI notices it: an observer hears a unit
// within hearing x loudness x sound modifier and sees it within sight x
// visibility x vision modifier. Visibility and loudness are the unit's
// effective sight and hearing percepts; the modifiers follow its pose and
// movement. There is no stealth skill term (the game's content leaves that
// skill unused); sneaking means crawling or lying down.
enum Pose : std::uint8_t { pose_lay = 0, pose_crawl, pose_stand, pose_run, pose_rest };
struct UnitSenses {
    float visibility = 1, loudness = 1;
    std::uint8_t pose = pose_stand;
    bool moving = false;
    bool attacking = false;    // current clip attacks or casts
};
// The game's pose modifiers: a unit standing still makes no movement sound;
// moving lying down is x0.4, crawling x0.2, running x2. Lying down is half as
// visible, crawling x0.75. An attack or cast is a x2 sound event.
inline float sound_modifier(const UnitSenses& u) noexcept {
    float m = 0;
    if (u.moving) m = u.pose == pose_lay ? .4f : u.pose == pose_crawl ? .2f : u.pose == pose_run ? 2.f : 1.f;
    return u.attacking ? std::max(m, 2.f) : m;
}
inline float vision_modifier(const UnitSenses& u) noexcept {
    return u.pose == pose_lay ? .5f : u.pose == pose_crawl ? .75f : 1.f;
}
// Critter sight as a share of its hearing reach (flee_r): a still unit is seen
// at the prototype's former idle distance.
constexpr float kCritterSight = .7f;
// Threat reach: the larger of the critter hearing the unit and seeing it.
inline float threat_reach(float hearing_r, const UnitSenses& u) noexcept {
    return std::max(hearing_r * u.loudness * sound_modifier(u),
                    kCritterSight * hearing_r * u.visibility * vision_modifier(u));
}

// units.udb Scrab (crawler), with Crab00/01/02 skins sharing this model.
inline bool crawler_model(const char* name) noexcept {
    return name && clip_prefix(name, "unanwicr") && name[8] == '\0';
}

// units.udb has blood but no undead flag. Base/Dejavu undead model families
// (including red-blood Dejavu undead soldiers) remain explicitly excluded.
inline bool undead_model(const char* name) noexcept {
    if (!name) return false;
    for (const char* prefix : {"unmoba", "unmosh", "unmosk", "unmozo", "unmocu", "unhusk", "unhuzf", "unhuzm"})
        if (clip_prefix(name, prefix)) return true;
    return false;
}

// ---------------------------------------------------------------- Dead City wall ring
struct Point { float x = 0, y = 0; };
constexpr float kWallLink = 32.f;      // pieces closer than this form one wall
// The town wall of a zone from its MOB wall (stwa*) and gate (stga*) pieces:
// the gated group of single-linkage clusters closed by its convex hull and
// pushed out by margin so the wall itself counts as inside. Empty: no ring.
inline std::vector<Point> wall_ring(const std::vector<std::pair<bool, Point>>& pieces, float margin = 4.f) {
    const std::size_t n = pieces.size();
    if (!n) return {};
    std::vector<std::size_t> root(n);
    for (std::size_t i = 0; i < n; ++i) root[i] = i;
    const auto find = [&](std::size_t i) { while (root[i] != i) { root[i] = root[root[i]]; i = root[i]; } return i; };
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j) {
            const float dx = pieces[i].second.x - pieces[j].second.x, dy = pieces[i].second.y - pieces[j].second.y;
            if (dx * dx + dy * dy < kWallLink * kWallLink) root[find(i)] = find(j);
        }
    std::vector<std::size_t> size(n, 0);
    std::vector<bool> gated(n, false);
    for (std::size_t i = 0; i < n; ++i) { ++size[find(i)]; if (pieces[i].first) gated[find(i)] = true; }
    std::size_t best = n;
    for (std::size_t i = 0; i < n; ++i) if (gated[i] && (best == n || size[i] > size[best])) best = i;
    if (best == n) return {};
    std::vector<Point> points;
    for (std::size_t i = 0; i < n; ++i) if (find(i) == best) points.push_back(pieces[i].second);
    std::sort(points.begin(), points.end(), [](Point a, Point b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    points.erase(std::unique(points.begin(), points.end(), [](Point a, Point b) { return a.x == b.x && a.y == b.y; }), points.end());
    if (points.size() < 3) return {};
    const auto turn = [](Point o, Point a, Point b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<Point> hull;
    for (int pass = 0; pass < 2; ++pass) {
        const std::size_t start = hull.size();
        for (std::size_t k = 0; k < points.size(); ++k) {
            const Point p = pass ? points[points.size() - 1 - k] : points[k];
            while (hull.size() >= start + 2 && turn(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        hull.pop_back();
    }
    float cx = 0, cy = 0;
    for (const auto& p : hull) { cx += p.x; cy += p.y; }
    cx /= float(hull.size()); cy /= float(hull.size());
    for (auto& p : hull) {
        const float dx = p.x - cx, dy = p.y - cy, d = std::sqrt(dx * dx + dy * dy);
        if (d > 0) { p.x += dx / d * margin; p.y += dy / d * margin; }
    }
    return hull;
}
inline bool inside_ring(const std::vector<Point>& poly, float x, float y) noexcept {
    bool c = false;
    for (std::size_t i = 0, j = poly.size() ? poly.size() - 1 : 0; i < poly.size(); j = i++)
        if ((poly[i].y > y) != (poly[j].y > y) &&
            x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) c = !c;
    return c;
}

// ---------------------------------------------------------------- carrion
// units.udb race field 40: 1/5 red, 2/6 green, 3/7 blue, 4/8 black;
// 5-8 spawn hit particles without ground bloodprints. Unknown/none stays out.
// In Dejavu's units.udb, 1 is every mammal and humanoid (humans, orcs,
// boars, wolves, wargs, deer, hares, bats, rats, ogres, trolls, goblins);
// 5 is dragons and the lava elemental. 0 is banshees, shadows and fungi, 8
// golems, skeletons and earth elementals, 6-7 wisps and ice elementals:
// nothing to eat. Green 2 and blue 3 (spiders, snakes, toads, beholders,
// octopi, unicorns) stay out, as do the undead model families (undead_model).
inline bool carrion_edible(std::uint32_t blood, bool undead) noexcept {
    return !undead && (blood == 1 || blood == 5);
}
constexpr float kCarrionDelayMin = 60.f, kCarrionDelayMax = 120.f; // game seconds after the death
// Scavenger of a corpse, or bird_none: none underground, none inside the
// Dead City wall; vultures in Suslanger, crows in Ingos, ravens in Gipat,
// on the Dead City outskirts and near the undead (user, 2026-10-03).
inline Bird carrion_bird(ambient::ZoneClass zone, bool near_undead, bool inside_wall) noexcept {
    if (zone == ambient::zone_cave || zone == ambient::zone_gipat_cave) return bird_none;
    if (zone == ambient::zone_dead_city && inside_wall) return bird_none;
    if (zone == ambient::zone_desert) return bird_vulture;
    if (zone == ambient::zone_snow && !near_undead) return bird_crow;
    return bird_raven;
}
// A corpse attracts its flock once it is older than its own delay
// (60-120 s of game time, by a per-corpse random value u in [0,1)).
inline bool carrion_due(float corpse_age, float u) noexcept {
    return corpse_age >= kCarrionDelayMin + (kCarrionDelayMax - kCarrionDelayMin) * u;
}
// Corvids and vultures already near a death come to it sooner (30-60 s),
// under the same landing rule; kNearDeath is how near (m) they must be.
constexpr float kCarrionNearDelayMin = 30.f, kCarrionNearDelayMax = 60.f, kNearDeath = 40.f;
inline bool carrion_near_due(float corpse_age, float u) noexcept {
    return corpse_age >= kCarrionNearDelayMin + (kCarrionNearDelayMax - kCarrionNearDelayMin) * u;
}

// ---------------------------------------------------------------- perches
// The Suslanger cactus models (shipped MOB census: nafltr71 saguaro, nafltr72
// prickly pear); vultures perch on them as small birds and corvids on trees.
inline bool cactus_model(const char* model) noexcept {
    return model && (std::strcmp(model, "nafltr71") == 0 || std::strcmp(model, "nafltr72") == 0);
}
// A perch candidate on a tree or cactus model (model space, z up): a point on
// one of its drawn triangles. bark: an opaque bark triangle or a branch-coloured
// texel of a cut-out card; up: an upward-facing bark triangle (a branch's top
// side, a cactus cap); part_top: the highest point of its part.
struct PerchCandidate { float p[3]{}; float part_top = 0; bool bark = false, up = false; };
enum PerchKind : std::uint8_t { perch_branch = 0, perch_edge, perch_fill };
struct PerchPick { std::uint32_t index = 0; PerchKind kind = perch_fill; };
constexpr unsigned kPerchCount = 6;
// Up to kPerchCount spread-out perches of one model, once per model: four on
// branches in the upper crown (above 40 % of the height), two on the crown's
// outer edge (65 % of its radius out), then fill. Each further pick
// maximises its distance to the earlier ones (at least 12 % of the height)
// plus half its score (height, reach, bark). Cactus perches are part tops
// (caps and arm tips) only. A solid model (no cut-out surface: a stump or a
// log) is perched on its top surface only: up-facing points in its upper 60 %
// with nothing of the model above them (no candidate within 10 cm across
// stands 5 cm higher), never the ground-level skirt beside it.
inline std::vector<PerchPick> select_perches(const std::vector<PerchCandidate>& c, bool cactus, bool solid = false) {
    std::vector<PerchPick> picks;
    if (c.empty()) return picks;
    float zmin = c[0].p[2], zmax = c[0].p[2];
    for (const auto& k : c) { zmin = std::min(zmin, k.p[2]); zmax = std::max(zmax, k.p[2]); }
    const float H = std::max(zmax - zmin, 1e-3f);
    float ax = 0, ay = 0; unsigned low = 0;   // trunk axis: the low candidates' centre
    for (const auto& k : c) if (k.p[2] - zmin < .25f * H) { ax += k.p[0]; ay += k.p[1]; ++low; }
    if (low) { ax /= float(low); ay /= float(low); }
    std::vector<float> r(c.size()), score(c.size());
    float reach = 1e-6f;
    for (std::size_t i = 0; i < c.size(); ++i) { r[i] = std::hypot(c[i].p[0] - ax, c[i].p[1] - ay); reach = std::max(reach, r[i]); }
    for (std::size_t i = 0; i < c.size(); ++i)
        score[i] = .6f * (c[i].p[2] - zmin) / H + .4f * r[i] / reach + (c[i].bark ? .3f : 0.f);
    const float gap = .12f * H;
    std::vector<bool> top(c.size(), true);
    if (solid)
        for (std::size_t i = 0; i < c.size(); ++i)
            for (std::size_t j = 0; j < c.size() && top[i]; ++j)
                top[i] = !(c[j].p[2] > c[i].p[2] + .05f && std::hypot(c[j].p[0] - c[i].p[0], c[j].p[1] - c[i].p[1]) < .1f);
    const auto pool = [&](unsigned which, std::size_t i) {
        const auto& k = c[i];
        const bool hi = k.p[2] - zmin >= .4f * H, tip = !cactus || k.p[2] >= k.part_top - .08f * H;
        if (solid && !(k.up && hi && top[i])) return false;
        switch (which) {
        case 0: return k.bark && k.up && hi && tip;
        case 1: return ((k.bark && k.up) || !k.bark) && hi && tip && r[i] >= .65f * reach;
        case 2: return k.bark && k.up && tip;
        case 3: return hi;
        default: return true;
        }
    };
    static constexpr unsigned quota[5]{4, 2, kPerchCount, kPerchCount, kPerchCount};
    static constexpr PerchKind kinds[5]{perch_branch, perch_edge, perch_fill, perch_fill, perch_fill};
    for (unsigned which = 0; which < (cactus ? 3u : 5u) && picks.size() < kPerchCount; ++which)
        for (unsigned taken = 0; taken < quota[which] && picks.size() < kPerchCount; ++taken) {
            std::size_t best = c.size(); float value = -1e9f;
            for (std::size_t i = 0; i < c.size(); ++i) {
                if (!pool(which, i)) continue;
                float v = score[i];
                if (!picks.empty()) {
                    float d = std::numeric_limits<float>::infinity();
                    for (const auto& p : picks) {
                        const auto& q = c[p.index];
                        d = std::min(d, std::sqrt((c[i].p[0] - q.p[0]) * (c[i].p[0] - q.p[0]) + (c[i].p[1] - q.p[1]) * (c[i].p[1] - q.p[1]) +
                                                  (c[i].p[2] - q.p[2]) * (c[i].p[2] - q.p[2])));
                    }
                    if (d < gap) continue;
                    v = d / H + .5f * score[i];
                }
                if (v > value) { value = v; best = i; }
            }
            if (best == c.size()) break;
            picks.push_back({std::uint32_t(best), kinds[which]});
        }
    return picks;
}

// ---------------------------------------------------------------- hashing
inline std::uint64_t splitmix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
constexpr std::uint64_t key(const char* s) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ull;
    while (*s) h = (h ^ static_cast<unsigned char>(*s++)) * 0x100000001B3ull;
    return h;
}
// Stateless, order-independent random value in [0,1) of a seed and keys.
inline float hash01(std::uint64_t seed, std::uint64_t a, std::uint64_t b = 0, std::uint64_t c = 0) noexcept {
    std::uint64_t h = splitmix(seed ^ a);
    h = splitmix(h ^ b);
    h = splitmix(h ^ c);
    return float(h >> 40) * (1.f / 16777216.f);
}

// ---------------------------------------------------------------- critter spots
// The game's terrain soundscape: every 32 m sector plays the ambient set of
// its dominant terrain type, counting its base and layer tiles with water,
// road, ice, lava and swamp x3, rock x1.5 and undefined x0; ties keep the
// lower type. (Shipped gameplay MOBs author no other ambient emitters: their
// Sound objects are campfires.)
inline std::uint8_t sector_sound(const std::uint32_t (&tiles)[16]) noexcept {
    static constexpr float weight[16]{1, 1, 1, 1, 1.5f, 1, 3, 3, 0, 1, 3, 1, 1, 3, 3, 1.5f};
    std::uint8_t best = 0;
    float best_weight = -1;
    for (unsigned t = 0; t < 16; ++t)
        if (float(tiles[t]) * weight[t] > best_weight) { best_weight = float(tiles[t]) * weight[t]; best = std::uint8_t(t); }
    return best;
}
// Species preference of a spot by its sector's soundscape: swamp and water
// sectors favour frogs and crabs, open meadows resting small-bird flocks,
// stony ground lizards, snakes and centipedes.
inline float sound_bias(Species s, std::uint8_t sound) noexcept {
    using namespace ambient;
    const bool wet = sound == tt_swamp || sound == tt_water;
    const bool meadow = sound == tt_grass || sound == tt_field || sound == tt_drygrass || sound == tt_snow || sound == tt_snowballs;
    const bool stony = sound == tt_ground || sound == tt_stone || sound == tt_rock || sound == tt_highrock || sound == tt_sand;
    if (s == sp_frog || s == sp_crab) return wet ? 3.f : 1.f;
    if (s == sp_bird || s == sp_snowbird) return meadow ? 2.f : 1.f;
    if (s == sp_lizard || s == sp_snake || s == sp_scolopendra) return stony ? 1.5f : 1.f;
    return 1.f;
}

constexpr float kSpotDensity = .6f;       // share of 32 m sectors that hold a spot
constexpr float kFireKeepout = 8.f;       // metres from a campfire sound
constexpr unsigned kSpotTries = 8;
// The one candidate spot of a terrain sector (16 x 16 area tiles of 2 m), a
// pure function of the map seed, the sector and the habitat grid: a land
// tile (not water or lava) away from campfires, or none. The same map always
// has the same spots.
inline bool sector_spot(std::uint64_t map_seed, std::uint32_t sx, std::uint32_t sy, const std::uint8_t* area,
                        std::uint32_t tiles_x, std::uint32_t tiles_y, const std::vector<Point>& fires, Point& out) noexcept {
    const std::uint64_t cell = (std::uint64_t(sy) << 32) | sx;
    if (!area || hash01(map_seed, cell, key("spot")) >= kSpotDensity) return false;
    for (unsigned k = 0; k < kSpotTries; ++k) {
        const std::uint32_t tx = sx * 16u + std::min(15u, unsigned(hash01(map_seed, cell, k, key("tx")) * 16));
        const std::uint32_t ty = sy * 16u + std::min(15u, unsigned(hash01(map_seed, cell, k, key("ty")) * 16));
        if (tx >= tiles_x || ty >= tiles_y) continue;
        const unsigned a = area[std::size_t(ty) * tiles_x + tx] & 15u;
        if (a == ambient::area_water || a == ambient::area_lava) continue;
        const Point p{(float(tx) + .2f + .6f * hash01(map_seed, cell, k, key("jx"))) * 2,
                      (float(ty) + .2f + .6f * hash01(map_seed, cell, k, key("jy"))) * 2};
        bool fire = false;
        for (const auto& f : fires) fire = fire || std::hypot(f.x - p.x, f.y - p.y) < kFireKeepout;
        if (fire) continue;
        out = p;
        return true;
    }
    return false;
}

// A disturbed spot stays empty for this long (game seconds), then may be
// found occupied again on a later approach.
constexpr float kSpotQuietMin = 90.f, kSpotQuietMax = 180.f;
constexpr float kSpotChance = .6f;        // an approached, rested spot is occupied
constexpr float kSpotBand = 1.4f;         // approach band just outside the view edge (NDC)
constexpr float kSpotForget = 2.f;        // beyond this (NDC) a resident is dropped, a spot re-armed
// On-screen budget shared by director events and spot residents (carrion
// flocks keep their own); scaled by the critters setting.
inline unsigned critter_budget(int level) noexcept { return level < 2 ? 2u : 3u; }

// ---------------------------------------------------------------- showcase (debug)
// critter_showcase: every walking species once, a small flock of every bird
// variant, a vulture group on the ground and a vulture pair on its thermal,
// laid out in a camera-aligned ground frame (metres; x to the screen's right,
// y away from the camera, origin at the row's centre). Purely visual.
struct ShowcaseSlot { Species species; Bird bird; std::uint8_t count; bool flying; float x, y; };
inline std::vector<ShowcaseSlot> showcase_layout(float spacing) {
    std::vector<ShowcaseSlot> out;
    const Species ground[] = {sp_rat, sp_snowmouse, sp_spider, sp_larva, sp_frog, sp_crab,
                              sp_lizard, sp_scolopendra, sp_dungbeetle, sp_snake};
    constexpr unsigned n = sizeof(ground) / sizeof(*ground);
    for (unsigned i = 0; i < n; ++i)
        out.push_back({ground[i], bird_none, 1, false, (float(i) - (n - 1) * .5f) * spacing, 0});
    const std::pair<Species, Bird> birds[] = {{sp_bird, bird_finch}, {sp_bird, bird_sparrow}, {sp_bird, bird_bunting},
                                              {sp_snowbird, bird_snowbird}, {sp_raven, bird_crow}, {sp_raven, bird_raven}};
    for (unsigned i = 0; i < 6; ++i)
        out.push_back({birds[i].first, birds[i].second, 3, false, (float(i) - 2.5f) * 1.6f * spacing, -1.6f * spacing});
    out.push_back({sp_vulture, bird_vulture, 3, false, 0, -3.4f * spacing});
    out.push_back({sp_vulture, bird_vulture, 2, true, 0, 0});   // circles the showcase at thermal height
    out.push_back({sp_bat, bird_bat, 6, true, 3.f * spacing, -3.4f * spacing});   // a small swarm
    return out;
}

} // namespace owned::critters
