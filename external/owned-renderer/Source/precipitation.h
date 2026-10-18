#pragma once
// Owned precipitation policy (renderer_precipitation.cpp draws it): which
// weather falls, how strongly, and how many drops a quality level spends.
//
// Game contract (shipped game.exe, issue #166): EWeather is clear 0, rain 1,
// snow 2. Gameplay reads only that state (vision bonus 0.9 in rain or snow,
// RecalcNightPower 0058CCD0); the client's 0..64 power is a visual fade of
// 20 (rain) or 40 (snow) segments. Strength is therefore a visual choice
// (user decision 2026-10-13): presence always follows the game's own fade,
// so drops appear and stop with the gameplay change; "Varies" picks each
// shower's strength from its onset tick, and it breathes on game time.
#include "world_fog.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace owned::precipitation {

// kind_sand: the renderer's visual sand storm (no game weather state; desert
// zones, driven by the shared wind's windy spells; user decision 2026-10-13).
enum Kind : int { kind_none = 0, kind_rain = 1, kind_snow = 2, kind_sand = 3 };

// Drops per square metre of ground at full strength (each falls through the
// whole column above it), and ground splashes per square metre per second
// (rain only; none on Low). 0 off, 1 low .. 4 ultra.
struct Quality { float rain_density = 0.f, snow_density = 0.f; bool sheets = false; float splash_rate = 0.f; float sand_density = 0.f; };
inline Quality quality_level(int level) noexcept {
    switch (level) {
    // Sand: many fine grains (9b32ff4a user note: coarse and blurry).
    case 1: return {4.f, 8.f, false, 0.f, 7.f};
    case 2: return {8.f, 14.f, true, 1.5f, 14.f};
    case 3: return {14.f, 22.f, true, 3.f, 20.f};
    case 4: return {22.f, 32.f, true, 5.f, 32.f};
    default: return {};
    }
}

// Sand storm level from the shared wind's speed multiplier (weather_wind.h:
// about .85 calm, 1.45 in a windy spell, up to 2.4 in storms): none below
// 1.15, a light dusty haze in ordinary windy spells, a full storm from 1.75.
inline float sand_storm_level(float wind_speed) noexcept {
    const float t = std::clamp((wind_speed - 1.15f) / .6f, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline std::uint32_t hash32(std::uint32_t x) noexcept {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

// Strength levels of the setting (1 light, 2 medium, 3 heavy).
inline float fixed_strength(int level) noexcept {
    return level <= 1 ? .3f : level == 2 ? .6f : 1.f;
}

// setting: 0 varies, 1..3 fixed. A varying shower takes light, medium or
// heavy (30/40/30) from its onset tick and breathes slowly around it on game
// seconds; a reload that moves the onset may reseed it.
// "Varies" is a pure function of the saved world clock, so a reload brings
// back the same strength: the save holds the weather kind, the transition
// length (CWorldServer "fpWeatherComeTime" is the fade duration passed to
// SetWeather, not an onset time; WorldServer.cpp SetWeather/save) and the
// world time, but no onset. A light/medium/heavy (30/40/30) pick per 1.5
// game hours, crossfaded smoothly, breathes on game seconds.
inline float shower_strength(int setting, double game_hours, double seconds) noexcept {
    if (setting > 0) return fixed_strength(setting);
    const double cell = std::floor(game_hours / 1.5), t = game_hours / 1.5 - cell;
    const auto pick = [](double c) {
        const std::uint32_t seed = hash32(static_cast<std::uint32_t>(static_cast<std::int64_t>(c)) ^ 0x9e3779b9u);
        const unsigned p = seed % 10u;
        return p < 3 ? .3f : p < 7 ? .6f : 1.f;
    };
    const float s = static_cast<float>(t * t * (3.0 - 2.0 * t));
    const float base = pick(cell) + (pick(cell + 1) - pick(cell)) * s;
    const float breathe = static_cast<float>(.16 * std::sin(seconds * (6.2831853 / 97.0)) +
                                             .08 * std::sin(seconds * (6.2831853 / 31.0) + 1.7));
    return std::clamp(base * (1.f + breathe), .2f, 1.f);
}

// What falls this frame: kind and presence (the game's own fade, 0..1). The
// render-only F10 precip_test is already applied by the environment reader.
struct Fall { int kind = kind_none; float presence = 0.f; };
inline Fall current_fall(const WorldEnvironment& e) noexcept {
    if (!e.valid || e.cave) return {};
    if (e.rain_particles > 0.f) return {kind_rain, e.rain_particles};
    if (e.snow_particles > 0.f) return {kind_snow, e.snow_particles};
    return {};
}

} // namespace owned::precipitation
