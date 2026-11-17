#pragma once
// Critter director population and drawn instances for the F11 capture's
// normal_frame.json (the same state the F10 critter status line reports).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct CritterSummary {
    std::uint32_t active = 0;            // live ground critters (events and spot residents)
    std::uint32_t flock_active = 0, flock_birds = 0;   // ambient flock
    std::uint32_t spot_residents = 0;    // occupied world spots
    std::uint32_t carrion_flocks = 0, carrion_birds = 0;
    std::uint32_t drawn_instances = 0;   // instances submitted this frame (on or off screen)
    std::uint32_t on_screen_instances = 0;   // submitted instances whose body overlaps the viewport
    float min_px = 0, max_px = 0;        // on-screen body length of the on-screen instances (whole px:
                                         // no decimal point, so the C locale cannot matter)
    std::uint32_t drawn_species = 0;     // bit per critters::Species, on-screen instances only
    // town cats: live cats, the map's towns, the nearest town house (m, -1
    // none) and every resident's fixed identity (town, slot, coat, shape, size
    // in percent of the 0.55 m figure), whether it is alive, how often it has
    // come (spawns) and where it is (whole metres)
    std::uint32_t cats = 0, cat_towns = 0;
    float cat_nearest_house = -1;
    struct TownCat { std::uint32_t town, spawns, size_pct; std::uint8_t slot, shape; bool alive; std::int32_t x, y; char coat[8]; };
    std::vector<TownCat> town_cats;
    // Where the first submitted instances are: NDC x/y in percent (|v| > 100
    // is off screen), body px and height above the ground in decimetres.
    struct Probe { std::uint8_t species; std::int16_t ndc_x, ndc_y; std::uint16_t px; std::int16_t alt_dm; };
    static constexpr unsigned kProbes = 12;
    Probe probes[kProbes]{};
    std::uint32_t probe_count = 0;
    // Diagnostic labels: viewport fraction 0..1, top-left origin. Sized by live population.
    struct Label { float x, y; char text[64]; std::uint32_t color = 0; }; // color: 0 the default
    std::vector<Label> labels;
    // Carrion director (the F10 status line's carrion row): present/tracked
    // edible corpses, corpses still on their arrival timer and the soonest (s),
    // corpses due a flock, flock arrivals so far, and each active carrion
    // flock's state (0 ground, 1 circling, 2 landing, 3 leaving, 4 gone), slots
    // reserved, last refused landing check (1 no slots, 2 threat, 3 quiet wait,
    // 4 slots lost on approach, 5 landing wait expired), threat margin (m),
    // quiet/scattered/landing-wait timers (s), birds and nearest party member (m).
    std::uint32_t corpses_present = 0, corpses_tracked = 0, corpse_timers = 0, corpses_due = 0;
    float corpse_next_s = 0;
    std::uint32_t carrion_arrivals = 0;
    struct CarrionFlock { std::uint8_t state, refused, count; bool land_ok; float margin, quiet, scattered, wait, party; };
    CarrionFlock carrion_flock[2]{};
};

// Names in critters::Species order.
inline constexpr const char* kCritterSpeciesNames[] = {
    "rat", "spider", "larva", "frog", "lizard", "snake", "centipede", "beetle",
    "bird", "raven/crow", "vulture", "crab", "snowbird", "mouse", "bat", "fish", "squirrel", "prairie dog", "cat"};

// Player switches per critter type, in critters::Species order (issue #126):
// key (boolean, on) and weight_key (integer percent, 100) in eid3d11.ini. The
// weight multiplies the species' own spawn weight, so it changes how often the
// type is picked against the others that fit the place; the on-screen budget
// stays the same. Off or 0 % keeps the type away, carrion flocks included.
struct CritterTypeSetting { const char* key; const char* weight_key; const char* label; const char* tooltip; };
inline constexpr CritterTypeSetting kCritterTypeSettings[] = {
    {"critter_rats", "critter_rats_weight", "Rats", "Rats in caves and in the city of Suslanger."},
    {"critter_spiders", "critter_spiders_weight", "Spiders", "Spiders in caves and the Dead City."},
    {"critter_larvae", "critter_larvae_weight", "Crawler larvae", "Larvae crawling around a live crawler."},
    {"critter_frogs", "critter_frogs_weight", "Frogs", "Frogs by swamps and water."},
    {"critter_lizards", "critter_lizards_weight", "Lizards", "Lizards on dry ground and sand, by day."},
    {"critter_snakes", "critter_snakes_weight", "Snakes", "Snakes in grass, on stony ground and in the Dead City; rare."},
    {"critter_scolopendras", "critter_scolopendras_weight", "Scolopendras", "Scolopendras in Suslanger."},
    {"critter_dung_beetles", "critter_dung_beetles_weight", "Dung beetles", "Dung beetles in Suslanger, by day."},
    {"critter_small_birds", "critter_small_birds_weight", "Small birds",
     "Flocks of finches, sparrows and buntings on meadows, by day."},
    {"critter_corvids", "critter_corvids_weight", "Ravens and crows",
     "Raven flocks outside the Dead City wall, single ravens and crows in trees, and the ravens and crows that come to corpses."},
    {"critter_vultures", "critter_vultures_weight", "Vultures",
     "Vultures circling over Suslanger or sitting on cacti, and the vultures that come to corpses there."},
    {"critter_crabs", "critter_crabs_weight", "Shore crabs", "Crabs on sandy shores."},
    {"critter_snow_birds", "critter_snow_birds_weight", "Snow birds", "Small bird flocks in Ingos, by day."},
    {"critter_snow_mice", "critter_snow_mice_weight", "Snow mice", "Mice in the snow of Ingos."},
    {"critter_bats", "critter_bats_weight", "Cave bats", "Bat swarms flying past moving heroes in caves."},
    {"critter_fish", "critter_fish_weight", "Fish",
     "Schools of fish in rivers, lakes, swamps and coastal water. They scatter when someone comes close."},
    {"critter_squirrels", "critter_squirrels_weight", "Squirrels",
     "Squirrels near trees in Gipat (red-brown) and Ingos (light grey), by day."},
    {"critter_prairie_dogs", "critter_prairie_dogs_weight", "Prairie dogs",
     "Prairie dogs in Suslanger, by day, away from monsters; they stand up to look round and drop into their burrows."},
    {"critter_cats", "critter_cats_weight", "Cats", "One resident cat per town of Ingos and Suslanger (two in a city), always the same cat in the same town; it wanders the town, sits, grooms, suns itself and hunts mice, and keeps its distance from people."},
};
static_assert(sizeof(kCritterTypeSettings) / sizeof(*kCritterTypeSettings) ==
              sizeof(kCritterSpeciesNames) / sizeof(*kCritterSpeciesNames), "one setting per critter species");
inline constexpr int kCritterWeightDefault = 100, kCritterWeightMaximum = 1000;   // percent

inline void append_critter_summary_json(std::string& out, const CritterSummary& c) {
    constexpr unsigned names = sizeof(kCritterSpeciesNames) / sizeof(*kCritterSpeciesNames);
    char line[448];
    std::snprintf(line, sizeof(line),
        ",\n  \"critters\":{\"active\":%u,\"flocks\":{\"active\":%u,\"birds\":%u},\"spot_residents\":%u,"
        "\"carrion_flocks\":%u,\"carrion_birds\":%u,\"drawn_instances\":%u,\"on_screen_instances\":%u,"
        "\"min_px\":%.0f,\"max_px\":%.0f,\"species\":[",
        c.active, c.flock_active, c.flock_birds, c.spot_residents, c.carrion_flocks, c.carrion_birds,
        c.drawn_instances, c.on_screen_instances, double(c.min_px), double(c.max_px));
    out += line;
    bool first = true;
    for (unsigned s = 0; s < names; ++s) {
        if (!(c.drawn_species >> s & 1u)) continue;
        out += first ? "\"" : ",\"";
        out += kCritterSpeciesNames[s];
        out += '"';
        first = false;
    }
    std::snprintf(line, sizeof(line), "],\"cats\":{\"live\":%u,\"towns\":%u,\"nearest_house_m\":%.0f,\"residents\":[",
        c.cats, c.cat_towns, double(c.cat_nearest_house));
    out += line;
    for (std::size_t i = 0; i < c.town_cats.size(); ++i) {
        const auto& t = c.town_cats[i];
        std::snprintf(line, sizeof(line), "%s{\"town\":%u,\"slot\":%u,\"coat\":\"%s\",\"shape\":%u,\"size_pct\":%u,"
            "\"alive\":%u,\"spawns\":%u,\"x\":%d,\"y\":%d}", i ? "," : "", t.town, unsigned(t.slot), t.coat, unsigned(t.shape),
            t.size_pct, t.alive ? 1u : 0u, t.spawns, int(t.x), int(t.y));
        out += line;
    }
    out += "]},\"instances\":[";
    for (unsigned i = 0; i < c.probe_count && i < CritterSummary::kProbes; ++i) {
        const auto& p = c.probes[i];
        std::snprintf(line, sizeof(line), "%s{\"species\":\"%s\",\"ndc_pct\":[%d,%d],\"px\":%u,\"alt_dm\":%d}",
            i ? "," : "", p.species < names ? kCritterSpeciesNames[p.species] : "?", p.ndc_x, p.ndc_y, unsigned(p.px), p.alt_dm);
        out += line;
    }
    std::snprintf(line, sizeof(line), "],\"carrion\":{\"corpses\":%u,\"tracked\":%u,\"timers\":%u,\"next_s\":%.0f,"
        "\"due\":%u,\"arrivals\":%u,\"states\":[", c.corpses_present, c.corpses_tracked, c.corpse_timers,
        double(c.corpse_next_s), c.corpses_due, c.carrion_arrivals);
    out += line;
    for (unsigned i = 0; i < c.carrion_flocks && i < 2; ++i) {
        const auto& f = c.carrion_flock[i];
        std::snprintf(line, sizeof(line), "%s{\"state\":%u,\"land_ok\":%u,\"refused\":%u,\"margin\":%.1f,\"quiet\":%.1f,"
            "\"scattered\":%.1f,\"wait\":%.1f,\"birds\":%u,\"party\":%.1f}", i ? "," : "", unsigned(f.state), f.land_ok ? 1u : 0u,
            unsigned(f.refused), double(f.margin), double(f.quiet), double(f.scattered), double(f.wait), unsigned(f.count), double(f.party));
        out += line;
    }
    out += "]}}";
}
