#pragma once
// Wind trails: thin translucent streaks that trace gusts near the camera in
// windy weather. Pure presence, schedule and shape rules (no D3D), shared by
// the renderer pass (renderer_wind_trails.cpp) and its regression.
//
// Every trail is a fixed slot with a repeating game-time cycle: one spawn per
// cycle at a hashed time, a hashed life of 1-2.5 s, and an activation draw
// against the visible count the current wind wants. The schedule is a pure
// function of the slot, the seed and game seconds, so pause freezes the
// trails and game speed scales them; the renderer only latches where a trail
// starts (camera dependent) at its spawn.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "ambient_particles.h"
#include "weather_wind.h"
#include "world_fog.h"

namespace owned::wind_trails {

inline constexpr unsigned kMaxSlots = 48;
inline constexpr unsigned kRibbonSegments = 32;
inline constexpr float kCurveCurvatureLimit = .2f;
inline constexpr float kLoopCurvatureLimit = 2.f / 3.f;

// These scalar rules are compiled both as C++ (the regression) and HLSL
// (the shipped ribbon). Keep a single formula rather than a test replica.
#define WT_PATH_RULES \
inline float wtEaseIntegral(float s,float duration){ \
 float u=min(max(s/duration,0.f),1.f); \
 return duration*(u*u*u-.5f*u*u*u*u)+max(s-duration,0.f); \
} \
inline float wtTravel(float age,float life,float speed){ \
 float s=min(max(age,0.f),life); \
 return speed*(wtEaseIntegral(s,.35f)-wtEaseIntegral(s-(life-.6f),.6f)); \
} \
inline float wtAlong(float d,float radius){return radius==0.f?d:radius*sin(d/radius);} \
inline float wtSide(float d,float amp,float rate,float phase,float radius){ \
 return radius==0.f?amp*(sin(rate*d+phase)-sin(phase)):radius*(1.f-cos(d/radius)); \
}
#define WT_STRING_INNER(...) #__VA_ARGS__
#define WT_STRING(...) WT_STRING_INNER(__VA_ARGS__)
inline constexpr char kPathHlsl[] = WT_STRING(WT_PATH_RULES);
namespace path {
using std::min; using std::max; using std::sin; using std::cos;
WT_PATH_RULES
} // namespace path
#undef WT_STRING
#undef WT_STRING_INNER
#undef WT_PATH_RULES

struct Quality { unsigned slots = 0; float count_scale = 0.0f; };
// Levels of the "wind_trails" switch: 0 off, 1 default (3-12 visible), 2 dense.
inline Quality quality_level(int level) noexcept {
    if (level <= 0) return {};
    if (level == 1) return {24u, 1.0f};
    return {kMaxSlots, 2.0f};
}

// Windiness of the shared wind: effective force (map force raised to the wind
// floor) times the spell/storm speed and the current gust pulse.
inline float windiness(const WeatherWindGpu& w) noexcept { return w.force * w.speed * w.pulse; }

// Tuning. Calm spells run at speed ~.85, windy spells ~1.45, storms up to ~2.4.
inline constexpr float kCalmSpeed = .9f, kWindySpeed = 1.2f;
inline constexpr float kPresenceLow = .2f, kPresenceHigh = .35f;
inline constexpr float kIntensityLow = .3f, kIntensityHigh = 1.2f;

// Visible trails the wind wants at a quality level: none in caves, none on
// calm spells or in weak wind, 3 rising to 12 (times the level scale) as the
// wind strengthens and gusts.
inline float target_count(const WeatherWindGpu& w, ambient::ZoneClass zone, const Quality& q) noexcept {
    if (q.slots == 0 || ambient::cave_zone(zone)) return 0.0f;
    const float e = windiness(w);
    const float presence = fog_smoothstep(kCalmSpeed, kWindySpeed, w.speed) * fog_smoothstep(kPresenceLow, kPresenceHigh, e);
    if (!(presence > 0.0f)) return 0.0f;
    const float intensity = std::clamp((e - kIntensityLow) / (kIntensityHigh - kIntensityLow), 0.0f, 1.0f);
    return presence * (3.0f + 9.0f * intensity) * q.count_scale;
}

// Slot schedule (game seconds).
inline constexpr double kCycleSeconds = 3.0;
inline constexpr float kLifeMin = 1.0f, kLifeMax = 2.5f;
inline constexpr float kVisibleShare = (kLifeMin + kLifeMax) * .5f / static_cast<float>(kCycleSeconds);

inline float slot_random(std::uint32_t seed, unsigned slot, std::int64_t cycle, std::uint32_t salt) noexcept {
    return weather_wind_hash(seed ^ salt, cycle * 64 + static_cast<std::int64_t>(slot));
}

struct Cycle { std::int64_t index = 0; double spawn = 0.0; float life = 0.0f; };
// The slot's current cycle: spawn time and life. A trail ends before the next
// cycle of its slot begins.
inline Cycle slot_cycle(std::uint32_t seed, unsigned slot, double seconds) noexcept {
    const double offset = weather_wind_hash(seed ^ 0x2c1b3c6du, slot) * kCycleSeconds;
    const double k = std::floor((seconds + offset) / kCycleSeconds);
    Cycle c;
    c.index = static_cast<std::int64_t>(k);
    c.life = kLifeMin + (kLifeMax - kLifeMin) * slot_random(seed, slot, c.index, 0x9e3779b9u);
    c.spawn = k * kCycleSeconds - offset + slot_random(seed, slot, c.index, 0x7f4a7c15u) * (kCycleSeconds - c.life);
    return c;
}

// Whether a cycle draws a trail: the activation chance makes the expected
// visible count equal the target.
inline bool slot_active(std::uint32_t seed, unsigned slot, std::int64_t cycle, float target, const Quality& q) noexcept {
    if (q.slots == 0 || slot >= q.slots || !(target > 0.0f)) return false;
    const float chance = target / (static_cast<float>(q.slots) * kVisibleShare);
    return slot_random(seed, slot, cycle, 0x94d049bbu) < chance;
}

// Gust fronts of the grass (ground_cover_interaction.h gcWindSway: eight-metre
// fronts travelling downwind at ground_cover.h kSwayTravel, 1.9 rad per game
// second); 1 on a front, 0 between.
inline float gust_front(float x, float y, const WeatherWindGpu& w, double seconds) noexcept {
    const double along = x * w.direction[0] + y * w.direction[1];
    return static_cast<float>(.5 + .5 * std::sin(along * .78539816 - seconds * 1.9));
}
// Spawn candidates are kept in proportion to this weight.
inline float spawn_weight(float front) noexcept { return .2f + .8f * front * front; }

// Shape of one trail, world metres and game seconds.
struct Shape {
    float speed = 0.0f;       // cruise travel, eased at birth and death
    float height = 0.0f;      // above the ground under it
    float trail_time = 0.0f;  // visible length in seconds of head path
    float amp = 0.0f, rate = 0.0f, phase = 0.0f;  // lateral amplitude, spatial frequency, phase
    float lift = 0.0f;
    float loop_radius = 0.0f; // zero for S-curves; signed radius for a loose loop
    float width = 1.0f;       // width scale
};
inline Shape make_shape(std::uint32_t seed, unsigned slot, std::int64_t cycle, const WeatherWindGpu& w, float /*life*/) noexcept {
    const auto r = [&](std::uint32_t salt) { return slot_random(seed, slot, cycle, salt); };
    Shape s;
    s.speed = (1.0f + 1.5f * w.force) * w.speed * w.pulse * (.8f + .4f * r(0x11u));
    const float h = r(0x12u);
    s.height = .4f + 2.1f * h * h;
    s.trail_time = .8f + .4f * r(0x13u);
    s.amp = .3f + .4f * r(0x14u);
    s.rate = .3f + .2f * r(0x15u);
    s.phase = 6.2831853f * r(0x16u);
    s.lift = .04f + .06f * r(0x17u);
    // No additive curl competing with forward motion. The occasional circle
    // travels at the same eased speed as an S-curve, in either direction.
    s.loop_radius = r(0x1au) < .18f ? (1.5f + .5f * r(0x1bu)) * (r(0x1du) < .5f ? -1.f : 1.f) : 0.f;
    s.width = .7f + .6f * r(0x1eu);
    return s;
}

// Renderer-held state of one slot: the latched cycle and, while it draws,
// where its trail started.
struct Slot {
    std::int64_t cycle = INT64_MIN;
    bool live = false;
    double spawn = 0.0;
    float life = 0.0f;
    float origin[2]{};
    float direction[2]{};
    Shape shape{};
};

} // namespace owned::wind_trails
