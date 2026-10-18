#pragma once
// Renderer-owned ambient particles: pure area/zone classification and the
// emitter table. No D3D, no engine state; the renderer (renderer_ambient_
// particles.cpp) feeds it the immutable MPR/MOB source, the terrain wind and
// the game environment of world_fog.h (the one clock/weather/region/cave
// reader shared with the fog).
//
// Classification reuses the prototypes' habitat model:
//  * ETERRTYPE per 2 m atlas tile from the MPR (critters eiterrain.py, grass
//    prototype ground_cover.cpp); the liquid layer's own tile type wins
//    (grass prototype: "trust the tile type there").
//  * Trees: MOB flora "nafl*", bushes "naflbu*" and the desert set
//    (71-73, 77, 78, 82) excluded (grass prototype extract_patch.py).
//  * Zone class as critters behaviour.py Zone.klass: Dead City (zone9.mpr =
//    map.txt gz9g), caves (Gipat caves separate), snow (Ingos), desert
//    (Suslanger), outdoor.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "world_fog.h"

namespace owned::ambient {

// ETERRTYPE.
enum TerrainType : std::uint32_t {
    tt_grass = 0, tt_ground, tt_stone, tt_sand, tt_rock, tt_field, tt_water, tt_road, tt_undefined,
    tt_snow, tt_ice, tt_drygrass, tt_snowballs, tt_lava, tt_swamp, tt_highrock,
};

enum AreaClass : std::uint8_t {
    area_bare = 0,  // ground, stone, rock, road, highrock, undefined
    area_grass,
    area_dry,       // drygrass, field
    area_sand,
    area_snow,      // snow, ice, snowballs
    area_swamp,
    area_water,
    area_lava,
    area_count,
};

inline AreaClass area_from_terrain_type(std::uint32_t type) noexcept {
    switch (type) {
    case tt_grass: return area_grass;
    case tt_field: case tt_drygrass: return area_dry;
    case tt_sand: return area_sand;
    case tt_snow: case tt_ice: case tt_snowballs: return area_snow;
    case tt_swamp: return area_swamp;
    case tt_water: return area_water;
    case tt_lava: return area_lava;
    default: return area_bare;
    }
}

// Liquid layer tile: its own ETERRTYPE when it names a liquid; otherwise the
// liquid object decides (self-illuminated liquid is lava, as the liquid shader
// and ripple code treat it).
inline AreaClass liquid_area(std::uint32_t layer_type, float self_illumination) noexcept {
    if (layer_type == tt_lava || layer_type == tt_swamp || layer_type == tt_water)
        return area_from_terrain_type(layer_type);
    return self_illumination > 0.0f ? area_lava : area_water;
}

// Shipped MOB/model census: distinguish crowns from bare branch cards and
// stumps. Unknown models supply no litter; naming alone is not foliage evidence.
enum TreeKind : std::uint8_t { tree_none, tree_deciduous, tree_conifer, tree_bare };
inline TreeKind tree_kind(const char* model) noexcept {
    if (!model) return tree_none;
    for (const char* name : {"nafltr56", "nafltr57", "nafltr75", "nafltr76"})
        if (std::strcmp(model, name) == 0) return tree_deciduous;
    if (std::strcmp(model, "nafltr59") == 0) return tree_conifer;
    for (const char* name : {"nafltr20", "nafltr21", "nafltr22", "nafltr23", "nafltr68", "nafltr69",
                             "nafltr70", "nafltr74", "nafltr83", "nafltr86"})
        if (std::strcmp(model, name) == 0) return tree_bare;
    return tree_none;
}
inline bool leafy_tree_model(const char* model) noexcept { return tree_kind(model) == tree_deciduous; }

// Shipped MOB object names identify these two static models as mushrooms.
// Match canonical model keys, never display-name substrings or guessed sites.
inline bool mushroom_model(const char* model) noexcept {
    return model && (std::strcmp(model, "nafltr77") == 0 || std::strcmp(model, "nafltr78") == 0);
}

enum ZoneClass : std::uint8_t { zone_outdoor = 0, zone_dead_city, zone_cave, zone_gipat_cave, zone_snow, zone_desert, zone_count };
// terrain_basename: lower-case MPR name without directory/extension; region
// is fog_region() of the Lights<region>.ini name.
inline ZoneClass classify_zone(const char* terrain_basename, bool cave, int region) noexcept {
    if (terrain_basename && std::strcmp(terrain_basename, "zone9") == 0) return zone_dead_city;
    if (cave) return region == fog_region_gipat ? zone_gipat_cave : zone_cave;
    if (region == fog_region_ingos) return zone_snow;
    if (region == fog_region_suslanger) return zone_desert;
    return zone_outdoor;
}

enum Emitter : std::uint8_t {
    em_pollen = 0,  // pollen, seed fluff and grass chaff over open grass
    em_leaves,      // small falling leaves around trees
    em_dead_motes,  // Dead City: ash-grey motes, pale spores, drifting dust
    em_cave_dust,   // slow dust motes catching the light
    em_drips,       // sparse falling cave drips
    em_spores,      // faint glowing spores (Gipat caves, swamps, Dead City)
    em_embers,      // rising embers over lava
    em_ash,         // grey ash flakes near lava
    em_dust,        // wind-driven dust over sand and dry ground
    em_fireflies,   // night, Gipat swamps/meadows/woods
    em_snow_glitter,// calm-air snow flakes and ice glitter over snow (not weather snow)
    em_spray,       // small foam splashes thrown up where a fall lands on its pool
    em_mist,        // soft mist billowing out and up from where a fall lands
    em_fall_foam,   // foam clumps riding down a fall's sheet into the boil
    em_lip_spray,   // droplets thrown off where water goes over a fall's lip
    em_count,
};

// Per-tile affinity model shared by the CPU presence test and the GPU:
// a = area[class] * (1 + tree_mul*t) + tree_add*t*litter, t = min(trees/3, 1),
// litter = ground that keeps fallen leaves: not water, lava or snow (snow-
// tagged tiles never take leaf/tree-driven particles). Values already include
// the zone gate.
struct AffinityTable {
    std::array<float, area_count> area{};
    float tree_mul = 0.0f;
    float tree_add = 0.0f;
};

inline AffinityTable emitter_affinity_table(Emitter e, ZoneClass zone) noexcept {
    //                                    bare  grass dry   sand  snow  swamp water lava
    using A = std::array<float, area_count>;
    AffinityTable t{};
    const bool cave = zone == zone_cave || zone == zone_gipat_cave;
    switch (e) {
    case em_pollen:
        if (zone == zone_outdoor) t.area = A{.25f, 1.f, .8f, .1f, 0.f, .4f, .15f, 0.f};
        else if (zone == zone_desert) t.area = A{0.f, .3f, .3f, 0.f, 0.f, 0.f, 0.f, 0.f};
        t.tree_mul = -.4f;
        break;
    case em_leaves:
        // No autumn leaves in the snow region (Ingos): it gets snow glitter.
        if (zone == zone_outdoor || zone == zone_dead_city) t.tree_add = 1.f;
        else if (zone == zone_desert) t.tree_add = .3f;
        break;
    case em_dead_motes:
        if (zone == zone_dead_city) t.area = A{1.f, .8f, 1.f, 1.f, .5f, .8f, .5f, 1.f};
        break;
    case em_cave_dust:
        if (cave) t.area = A{1.f, 1.f, 1.f, 1.f, 1.f, .8f, .5f, .3f};
        break;
    case em_drips:
        if (cave) t.area = A{1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f};
        break;
    case em_spores:
        // Cave spores use only the preplaced mushroom source, never an area.
        if (zone == zone_dead_city) t.area = A{.35f, .35f, .35f, .2f, .2f, 1.f, .3f, 0.f};
        else if (zone == zone_outdoor) t.area = A{0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        break;
    case em_embers:
    case em_ash:
        t.area = A{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
        break;
    case em_dust:
        // Blown sand: the desert region's sand, with its bare/dry ground at
        // reduced strength; sand tiles elsewhere outdoors weaker still.
        if (zone == zone_desert) t.area = A{.35f, 0.f, .3f, 1.f, 0.f, 0.f, 0.f, 0.f};
        else if (zone == zone_dead_city) t.area = A{.2f, 0.f, .2f, .5f, 0.f, 0.f, 0.f, 0.f};
        else if (zone == zone_outdoor) t.area = A{0.f, 0.f, 0.f, .3f, 0.f, 0.f, 0.f, 0.f};
        break;
    case em_fireflies:
        // Region-gated by the caller (Gipat outdoor only; see environment_scale).
        if (zone == zone_outdoor) { t.area = A{0.f, .6f, .3f, 0.f, 0.f, 1.f, .5f, 0.f}; t.tree_add = .4f; }
        break;
    case em_snow_glitter:
        // The snow region everywhere on land, snow-tagged tiles elsewhere outdoors.
        if (zone == zone_snow) t.area = A{.35f, .2f, .2f, .2f, 1.f, .1f, .15f, 0.f};
        else if (!cave) t.area = A{0.f, 0.f, 0.f, 0.f, .5f, 0.f, 0.f, 0.f};
        break;
    case em_spray:
    case em_mist:
    case em_fall_foam:
    case em_lip_spray:
        // Open water everywhere; the renderer's per-tile impact source
        // (owned_terrain_liquid_flow) confines both to where falls land.
        t.area = A{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f};
        break;
    default: break;
    }
    return t;
}

inline float tile_affinity(const AffinityTable& t, AreaClass area, unsigned trees) noexcept {
    const float f = std::min(static_cast<float>(trees) / 3.0f, 1.0f);
    const float litter = area == area_water || area == area_lava || area == area_snow ? 0.0f : 1.0f;
    return std::clamp(t.area[area] * (1.0f + t.tree_mul * f) + t.tree_add * f * litter, 0.0f, 1.0f);
}

struct Weather { float rain = 0.0f, snow = 0.0f; };
// Rain and snow of the current weather blend: the current kind fades to the
// next by the pending change's progress, exactly as fog_precipitation().
inline Weather weather_amount(const WorldEnvironment& e) noexcept {
    const float p = std::clamp(e.weather_progress, 0.0f, 1.0f);
    auto kind = [p](int current, int next, int type) {
        return (current == type ? 1.0f - p : 0.0f) + (next == type ? p : 0.0f);
    };
    const float scale = 0.4f + 0.6f * e.precipitation_strength;  // light showers react less
    return {kind(e.weather_current, e.weather_next, 1) * scale, kind(e.weather_current, e.weather_next, 2) * scale};
}

// Rain and snow as the particles see them: the weather blend ramped like the
// rain fog (fog_ramp_precipitation, a quarter of a game hour), so particles
// thin out and return gradually instead of switching with the weather flag.
// Game time only: paused time holds the level; load/map jumps snap.
inline Weather ramp_weather(Weather level, Weather target, double elapsed_hours) noexcept {
    return {fog_ramp_precipitation(level.rain, target.rain, elapsed_hours),
            fog_ramp_precipitation(level.snow, target.snow, elapsed_hours)};
}

// Global (non-spatial) density multiplier from time of day and weather.
inline float environment_scale(Emitter e, ZoneClass zone, int region, float night, Weather w) noexcept {
    const float dry = 1.0f - w.rain;
    switch (e) {
    case em_pollen: return (1.0f - .75f * night) * (1.0f - .9f * w.rain) * (1.0f - w.snow);
    case em_leaves: return (1.0f - .4f * w.rain) * (1.0f - .7f * w.snow);
    case em_dead_motes: return 1.0f - .5f * w.rain;
    case em_cave_dust: case em_drips: return 1.0f;
    case em_spores: return (1.0f + .6f * night) * (zone == zone_outdoor || zone == zone_dead_city ? dry : 1.0f);
    case em_embers: return 1.0f - .2f * w.rain;
    case em_ash: return 1.0f - .6f * w.rain;
    // Wet sand does not blow: rain removes it almost entirely.
    case em_dust: return (1.0f - .5f * night) * (1.0f - .97f * w.rain) * (1.0f - .7f * w.snow);
    case em_fireflies:
        return region == fog_region_gipat && zone == zone_outdoor ? night * dry * (1.0f - w.snow) : 0.0f;
    // Calm air only: weather snow (precipitation) and rain replace it.
    case em_snow_glitter: return (1.0f - .5f * night) * (1.0f - .9f * w.rain) * (1.0f - .85f * w.snow);
    case em_spray: case em_mist: case em_fall_foam: case em_lip_spray: return 1.0f;
    default: return 0.0f;
    }
}

// Quality levels of the "ambient_particles" switch.
// radius: depth of the particle field beyond the camera focus (and half the
// world period of the field); density scales every emitter's column density.
struct Quality { float radius = 0.0f, density = 0.0f, decorative_density = 1.0f; };
inline Quality quality_level(int level) noexcept {
    if (level <= 0) return {};
    if (level == 1) return {20.0f, .5f, .2f};
    return {28.0f, .65f};
}

// Look and motion of one emitter. Distances in world units (metres), speeds
// per second of game time, sizes are quad widths.
struct EmitterStyle {
    float column_density;      // particles per square metre of ground at affinity 1
    float height_min, height_max;
    float size_min, size_max;
    float color_a[3], color_b[3];
    float alpha;
    float vertical_speed;      // + rises, - falls (wraps inside the height band)
    float wind_response;       // fraction of the wind drift
    float flutter;             // lateral wobble amplitude
    float flutter_rate;        // wobble angular speed
    float emissive;            // 0 lit by sun/ambient, 1 self-lit (additive glow)
    float stretch;             // >1: elongated along motion (drips, chaff)
    float spin;                // tumbling rate (leaves)
    float twinkle;             // brightness pulsing (fireflies, embers)
};

inline const EmitterStyle& emitter_style(Emitter e, ZoneClass zone = zone_outdoor) noexcept {
    // Cave spores have their own faint look; their source mask comes from the
    // preplaced mushrooms. Outdoor swamp/Dead City spores keep their old style.
    static const EmitterStyle mushroom_spores =
        {.3f, .2f, 2.4f, .05f, .08f, {.45f, .8f, .65f}, {.5f, .65f, .8f}, .18f, .08f, .1f, .15f, .4f, .5f, 1.f, .0f, .15f};
    if (e == em_spores && (zone == zone_cave || zone == zone_gipat_cave)) return mushroom_spores;
    static const EmitterStyle styles[em_count] = {
        // pollen: warm white/pale yellow fluff and green chaff flecks, half each
        // (sparse fluff: a third of its former count; the chaff keeps its count)
        {.125f, .2f, 3.0f, .12f, .18f, {1.f, .96f, .78f}, {.86f, .92f, .55f}, .8f, .05f, 1.0f, .35f, .9f, 0.f, 1.f, .0f, .0f},
        // leaves: olive, ochre and rust, tumbling as they fall
        {.12f, .0f, 9.0f, .21f, .3f, {.62f, .55f, .18f}, {.78f, .38f, .12f}, .95f, -.55f, .8f, .45f, 1.6f, 0.f, 1.f, 3.2f, .0f},
        // dead motes: ash grey to bone, slowly sinking
        {.4f, .1f, 5.0f, .09f, .165f, {.62f, .62f, .6f}, {.8f, .78f, .7f}, .55f, -.06f, .5f, .25f, .5f, 0.f, 1.f, .0f, .0f},
        // cave dust: sparse, small neutral motes lit by the scene, no sparkle
        {.04f, .2f, 4.5f, .035f, .06f, {.7f, .68f, .62f}, {.6f, .59f, .55f}, .25f, .01f, .1f, .12f, .35f, 0.f, 1.f, .0f, .0f},
        // drips: pale blue-white, fast streaks
        {.04f, .0f, 6.0f, .035f, .05f, {.8f, .88f, 1.f}, {.9f, .95f, 1.f}, .55f, -5.5f, .0f, .0f, .0f, 0.f, 9.f, .0f, .0f},
        // spores: soft blue-green glow, slowly rising
        {.15f, .2f, 4.0f, .09f, .15f, {.45f, .95f, .75f}, {.55f, .8f, 1.f}, .7f, .12f, .3f, .3f, .6f, 1.f, 1.f, .0f, .5f},
        // embers: orange to yellow sparks rising fast with turbulence
        {.5f, .0f, 7.0f, .075f, .135f, {1.f, .45f, .1f}, {1.f, .8f, .3f}, 1.f, 1.3f, .4f, .5f, 2.2f, 1.f, 1.6f, .0f, .8f},
        // ash: grey flakes drifting above lava
        {.2f, .5f, 8.0f, .105f, .18f, {.45f, .43f, .42f}, {.6f, .58f, .55f}, .7f, .25f, .5f, .4f, .8f, 0.f, 1.f, 1.2f, .0f},
        // sand: fine ochre grains hugging the ground (lifted by the gust rule
        // below, which also adds a few faint veils), streaked along the wind
        {.9f, .0f, 1.0f, .03f, .06f, {.88f, .74f, .52f}, {.76f, .62f, .44f}, .5f, .0f, 1.2f, .12f, 1.2f, 0.f, 1.6f, .0f, .0f},
        // fireflies: sparse yellow-green blinking glow (a third of the former count)
        {.017f, .4f, 2.5f, .12f, .18f, {.8f, 1.f, .35f}, {1.f, .95f, .45f}, 1.f, .0f, .05f, .8f, .35f, 1.f, 1.f, .0f, 1.f},
        // snow glitter: sparse white/ice-blue flakes drifting slowly, a faint twinkling sparkle
        {.1f, .1f, 4.0f, .05f, .09f, {.95f, .97f, 1.f}, {.82f, .9f, 1.f}, .8f, -.18f, .7f, .35f, .7f, .2f, 1.f, .0f, .7f},
        // spray: small white droplets in short ballistic arcs on the impact
        // water (height_max is the highest arc's peak) and foam flecks riding
        // the surface outward (2.4x size). No wind, no wobble.
        {6.f, .02f, .4f, .035f, .09f, {.97f, .98f, 1.f}, {.88f, .92f, .96f}, .8f, .0f, .0f, .0f, .0f, 0.f, 1.f, .0f, .0f},
        // mist: few large, very soft, depth-faded puffs veiling the foot of a
        // fall, billowing out (up to 0.7 m) and up to 3 m up the face, drifting
        // gently with the wind (6%), growing and thinning out.
        {4.f, .1f, 2.f, 1.1f, 2.1f, {.92f, .94f, .96f}, {.84f, .87f, .9f}, .4f, .3f, .06f, .1f, .3f, 0.f, 1.f, .0f, .0f},
        // fall foam: rounded clumps from a fall's lip riding down its sheet,
        // shrinking and dissolving into the boil (size shrinks to .65x).
        {10.f, .0f, .1f, .35f, .7f, {.82f, .85f, .88f}, {.72f, .76f, .8f}, .6f, .0f, .0f, .0f, .0f, 0.f, 1.f, .0f, .0f},
        // lip spray: small droplets thrown forward off a fall's lip.
        {3.f, .0f, .1f, .025f, .05f, {.95f, .97f, 1.f}, {.88f, .92f, .96f}, .5f, .0f, .0f, .0f, .0f, 0.f, 1.f, .0f, .0f},
    };
    return styles[e < em_count ? e : 0];
}

// Blown sand gusts (em_dust). Fronts kSandFrontSpacing apart travel downwind
// across the map's own wind direction; at a fixed point the front phase u
// (0..1) rises with game time. The CPU integrates the front position, so a
// changing wind speed never jumps the fronts. Mirrored by the HLSL in
// renderer_ambient_particles.cpp.
inline constexpr float kSandFrontSpacing = 14.0f;
inline constexpr float kSandHaze = .06f;  // density between gusts: a very faint haze
inline float sand_smoothstep(float a, float b, float x) noexcept {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// Fast arrival at the leading edge, long settling tail; 0 at both ends so the
// per-front amplitude can change between fronts without a seam.
inline float sand_gust_envelope(float u) noexcept {
    const float k = 1.0f - std::clamp(u, 0.0f, 1.0f);
    return std::min(1.0f, sand_smoothstep(0.0f, .07f, u) * k * k * std::sqrt(k) * 1.2f);
}
// Gust strength 0..1 from the shared wind: gust intensity (calm/windy/storm
// spells), the effective force (map force raised to the wind floor) and the
// global gust pulse.
inline float sand_gust_strength(float gust, float force, float pulse) noexcept {
    return std::clamp((.4f + .6f * gust) * (.5f + 1.5f * force) * (.6f + .4f * pulse), 0.0f, 1.0f);
}
// Sand sheet: strong gusts fill the gaps between streamers just above the ground.
inline float sand_sheet(float gust) noexcept { return sand_smoothstep(.45f, .8f, gust); }
// Particle density (fraction of the pool shown) at a point: faint haze plus
// the passing gust (strength * envelope * per-front amplitude) shaped into
// streamers (0..1 across the wind), merging into a sheet when strong.
inline float sand_gust_density(float gust, float streamer) noexcept {
    const float g = std::clamp(gust, 0.0f, 1.0f), sheet = sand_sheet(g);
    return kSandHaze + (1.0f - kSandHaze) * g * (streamer + (1.0f - streamer) * sheet);
}

// Waterfall emitters keep their density at every quality level (the low
// level's smaller density and radius still scale them).
inline bool waterfall_emitter(Emitter e) noexcept {
    return e == em_spray || e == em_mist || e == em_fall_foam || e == em_lip_spray;
}
// Pool size of one emitter at a quality level: the ground square of the
// volume times the emitter's peak column density. Sized by demand, no cap.
inline unsigned emitter_pool(Emitter e, const Quality& q, ZoneClass zone = zone_outdoor) noexcept {
    const float side = 2.0f * q.radius;
    // Default decorative atmosphere is sparse. High quality restores the old
    // density; waterfall spray keeps its density at every quality level.
    // Waterfall emitters fill only the map's waterfall tiles: their pool is
    // the particles of one 2 m tile.
    // Splashes fill 2 m impact tiles; fall foam, lip spray and mist one slot
    // per procedural fall (about 12 m^2 of sheet).
    if (e == em_spray) return static_cast<unsigned>(4.0f * emitter_style(e, zone).column_density * q.density + .5f);
    if (waterfall_emitter(e)) return static_cast<unsigned>(12.0f * emitter_style(e, zone).column_density * q.density + .5f);
    return static_cast<unsigned>(side * side * emitter_style(e, zone).column_density * q.density * q.decorative_density + .5f);
}

} // namespace owned::ambient
