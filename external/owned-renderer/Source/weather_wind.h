#pragma once
// One renderer wind for every wind-driven visual (tree sway, ambient
// particles, fog wisps, ground cover). Evaluated once per world frame on the
// CPU from the game's terrain wind and the shared environment reader of
// world_fog.h; consumers copy WeatherWindGpu into their own constant buffer.
//
//  * base: the map's terrain wind (direction, normalized force), which the
//    game keeps constant per map;
//  * calm and windy spells: smooth value noise over minutes of game time;
//  * storms: rain and snow ramp in and out over a quarter game hour (the fog's
//    ramp rule) and raise speed and gustiness;
//  * slight direction wander, wider in storms.
// Game time is the world segment clock: it stops when paused, runs faster at
// fast speed, and a load or map jump only re-seeds the noise position. The
// result is a pure function of the map's wind, region and the game clock, so
// every frame at the same game time gives the same wind.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "ambient_particles.h"
#include "world_fog.h"

namespace owned {

// Constant-buffer layout (two registers). HLSL: kWeatherWindHlsl.
struct WeatherWindGpu {
    float direction[2]{.7071068f, .7071068f}; // unit, downwind, with wander
    float speed = 1.0f;   // smooth multiplier of the map's wind: ~.85 calm, ~1.45 windy, up to ~2.4 in storms
    float gust = 0.0f;    // gust intensity 0..1: contrast of travelling gust fronts
    float force = 0.0f;   // effective wind force 0..1: the map's normalized force raised to the wind floor
    float pulse = 1.0f;   // current global gust multiplier (seconds scale) for consumers without spatial gusts
    float rain = 0.0f;    // ramped outdoor rain 0..1
    float seconds = 0.0f; // game-time seconds, wrapped every hour
};
static_assert(sizeof(WeatherWindGpu) == 32, "weather wind constants must match HLSL");
inline constexpr char kWeatherWindHlsl[] = R"(
struct WeatherWind{float2 direction;float speed;float gust;float force;float pulse;float rain;float seconds;};
// Bend strength of a wind speed: linear up to 1 (calm and mild wind keep the
// previous motion), then saturating toward 1.25 (a ~2.35 storm bends ~1.47x a
// calm .85). The ground cover's sway_drive uses the same curve on the CPU.
float windSwayStrength(float speed){
 const float knee=1,cap=1.25;float s=max(speed,0);
 return s<=knee?s:knee+(cap-knee)*(1-exp(-(s-knee)/(cap-knee)));
}
// Tree foliage wind drive at a travelling gust swell (0-1). The calm gust
// (.1) keeps its previous swell of the whole motion; stronger gusts add only
// treeSwayWindLean, a deeper steady lean carried by the slow swells, never a
// faster or wider shake. A storm's peak bend is ~1.7x calm instead of ~5x.
float treeSwayWindDrive(WeatherWind w,float swell){return windSwayStrength(w.speed)*(1+1.2*min(w.gust,.1)*swell);}
float treeSwayWindLean(WeatherWind w,float swell){return 1+.3*saturate((w.gust-.1)/.9)*swell;}
)";

// World segments to game seconds (the ambient particle and liquid clock scale).
inline constexpr double kWeatherSecondsPerSegment = .055;

struct WeatherWindState {
    WeatherWindGpu gpu{};
    double seconds = -1.0;      // last evaluated game seconds
    double hours = -1.0;        // last game hours (precipitation ramp)
    float rain = 0.0f, snow = 0.0f;
    float surface_rain = 0.0f; // short game-time smoothing for water impacts only
    double surface_rain_seconds = -1.0;
    double wisp[2]{};           // integrated fog-wisp drift, world units
};

// A full dry/wet change settles in 1.5 game seconds. Pause holds the value;
// invalid/indoor frames clear history.
inline float update_weather_surface_rain(WeatherWindState& s, const WorldEnvironment& env) noexcept {
    const double seconds = env.world_time * kWeatherSecondsPerSegment;
    const double elapsed = seconds - s.surface_rain_seconds;
    if (!env.valid || env.cave) {
        s.surface_rain = 0.0f;
        s.surface_rain_seconds = -1.0;
        return 0.0f;
    }
    if (s.surface_rain_seconds < 0.0 || elapsed < 0.0) {
        s.surface_rain = env.rain_particles;
    } else {
        const float step = static_cast<float>(elapsed / 1.5);
        s.surface_rain += std::clamp(env.rain_particles - s.surface_rain, -step, step);
    }
    s.surface_rain_seconds = seconds;
    return s.surface_rain;
}

inline float weather_wind_hash(std::uint32_t seed, std::int64_t lattice) noexcept {
    std::uint32_t h = seed ^ static_cast<std::uint32_t>(lattice) * 0x9e3779b1u ^
                      static_cast<std::uint32_t>(lattice >> 32) * 0x85ebca77u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return static_cast<float>(h >> 8) / 16777216.0f;
}
// Smooth 1D value noise in [0, 1) with a C2 quintic fade.
inline float weather_wind_noise(std::uint32_t seed, double x) noexcept {
    const double cell = std::floor(x);
    const auto i = static_cast<std::int64_t>(cell);
    const float f = static_cast<float>(x - cell), u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float a = weather_wind_hash(seed, i), b = weather_wind_hash(seed, i + 1);
    return a + (b - a) * u;
}

// Per-map seed from the map's own wind and region (both constant per map).
inline std::uint32_t weather_wind_seed(const float direction[2], float force, int region) noexcept {
    std::uint32_t bits[3]{};
    std::memcpy(&bits[0], &direction[0], 4); std::memcpy(&bits[1], &direction[1], 4); std::memcpy(&bits[2], &force, 4);
    std::uint32_t h = 0x811c9dc5u ^ static_cast<std::uint32_t>(region) * 0x27d4eb2du;
    for (auto v : bits) { h ^= v; h *= 0x01000193u; h ^= h >> 13; }
    return h;
}

// Tuning (game seconds / radians / multipliers).
inline constexpr double kWindSpellSeconds = 150.0, kWindSpellDetailSeconds = 50.0;
inline constexpr double kWindWanderSeconds = 80.0, kWindPulseSeconds = 5.0;
inline constexpr float kWindCalmSpeed = .85f, kWindWindySpeed = .6f, kWindStormSpeed = .9f;
inline constexpr float kWindWander = .2f, kWindStormWander = .15f;

// Some maps set their terrain wind force to exactly 0, which left grass and
// reeds frozen. The effective force is the map's normalized force raised to a
// gentle floor: 0 maps to the floor, forces at or above twice the floor are
// unchanged, and a quadratic blend in between keeps it smooth and monotonic.
// A floor of 0 gives the map's force unchanged.
inline constexpr float kDefaultWindFloor = .15f;
inline float weather_wind_effective_force(float map_force, float floor) noexcept {
    const float f = std::clamp(std::isfinite(map_force) ? map_force : 0.0f, 0.0f, 1.0f);
    const float lo = std::clamp(floor, 0.0f, .5f);
    if (lo <= 0.0f || f >= 2.0f * lo) return f;
    return lo + f * f / (4.0f * lo);
}

// Advances the wind to the environment's game time. variation=false gives the
// map's wind unchanged (speed 1, no gusts, no wander), the previous behaviour.
inline void update_weather_wind(WeatherWindState& s, const float base_direction[2], float base_force,
                                const WorldEnvironment& env, bool variation, float wind_floor = 0.0f) noexcept {
    const bool outdoor = env.valid && !env.cave;
    // Precipitation ramps like the fog's rain (game hours; jumps snap).
    const auto weather = ambient::weather_amount(env);
    if (env.valid) {
        const double elapsed = s.hours < 0.0 ? -1.0 : env.game_hours - s.hours;
        s.rain = fog_ramp_precipitation(s.rain, outdoor ? weather.rain : 0.0f, elapsed);
        s.snow = fog_ramp_precipitation(s.snow, outdoor ? weather.snow : 0.0f, elapsed);
        s.hours = env.game_hours;
    }
    const double seconds = env.valid ? env.world_time * kWeatherSecondsPerSegment : (s.seconds < 0.0 ? 0.0 : s.seconds);
    double dt = s.seconds < 0.0 ? 0.0 : seconds - s.seconds;
    if (!(dt >= 0.0 && dt < 2.0)) dt = 0.0;  // pause holds; loads and jumps do not drift
    s.seconds = seconds;

    float along[2]{.7071068f, .7071068f};
    const float length = std::sqrt(base_direction[0] * base_direction[0] + base_direction[1] * base_direction[1]);
    if (length > 1e-6f) { along[0] = base_direction[0] / length; along[1] = base_direction[1] / length; }
    WeatherWindGpu w{};
    w.force = weather_wind_effective_force(base_force, wind_floor);
    // Ramp history may outlive an outdoor frame; it cannot rain or drive a
    // storm indoors, or while the authoritative environment is unavailable.
    w.rain = outdoor ? s.rain : 0.0f;
    w.seconds = static_cast<float>(std::fmod(seconds, 3600.0));
    w.direction[0] = along[0]; w.direction[1] = along[1];
    if (variation) {
        const std::uint32_t seed = weather_wind_seed(base_direction, base_force, env.region);
        const float storm = outdoor ? std::clamp((std::max)(s.rain, .8f * s.snow), 0.0f, 1.0f) : 0.0f;
        const float spell = .65f * weather_wind_noise(seed, seconds / kWindSpellSeconds) +
                            .35f * weather_wind_noise(seed ^ 0x5bd1e995u, seconds / kWindSpellDetailSeconds + 17.0);
        const float windy = fog_smoothstep(.52f, .78f, spell);
        w.speed = kWindCalmSpeed + kWindWindySpeed * windy + kWindStormSpeed * storm;
        w.gust = std::clamp(.1f + .4f * windy + .6f * storm, 0.0f, 1.0f);
        const float g = weather_wind_noise(seed ^ 0x68e31da4u, seconds / kWindPulseSeconds);
        w.pulse = (std::max)(.5f, 1.0f + w.gust * .8f * (g * g - .33f));
        const float angle = (kWindWander + kWindStormWander * storm) *
                            (2.0f * weather_wind_noise(seed ^ 0xb5297a4du, seconds / kWindWanderSeconds + 5.0) - 1.0f);
        const float c = std::cos(angle), sn = std::sin(angle);
        w.direction[0] = c * along[0] - sn * along[1]; w.direction[1] = sn * along[0] + c * along[1];
        // Wisps drift downwind at ~.47 m/s times the current wind; the noise
        // is sampled at p + offset, so the offset runs upwind.
        const double drift = .47 * w.speed * w.pulse * dt;
        s.wisp[0] = std::fmod(s.wisp[0] - w.direction[0] * drift, 8192.0);
        s.wisp[1] = std::fmod(s.wisp[1] - w.direction[1] * drift, 8192.0);
    } else {
        // The previous fixed wisp drift of the fog shader, in world units.
        const double t = env.valid ? std::fmod(env.world_time, 100000.0) : 0.0;
        s.wisp[0] = t * (.002 / .09); s.wisp[1] = t * (.0012 / .09);
    }
    s.gpu = w;
}

}  // namespace owned
