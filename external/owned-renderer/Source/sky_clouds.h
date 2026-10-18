#pragma once

// Owned sky and volumetric clouds (issue #69). Replaces the game's sky pass:
// CWorldClient::Draw (00569E40) calls CSkyObject::Draw (004D96E0) at
// 00569FB7 when the world flag WC_RENDER_SKY (0x1000) is set; the bridge
// replaces that call and the renderer draws this sky behind the first world
// batch of the frame (depth test against the cleared far plane, no write).
//
// One weather/density field (world-anchored noise moved by the game's wind
// over game time) feeds the raymarched sky clouds, the reflection probe used
// by water and the world-space cloud shadow map that the world pixels and the
// fog volume sample. Time of day, weather, region, light colours and the fog
// colour come from the game through the fog's environment reader and the
// shared world lighting; nothing here reads game memory.
//
// Look and constants come from the approved offline prototype
// (Prototypes/sky_clouds on claude/sky-clouds-prototype, pass 2).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "world_fog.h"

namespace owned {

// Drift and shape evolution follow game seconds: pause holds and fast speed
// scales both. The menu uses this same client tick/fraction clock; the
// time-of-day scale/base can also change independently (scripted lighting).
// The caller clears the clock when no outdoor sky is prepared.
//
// The density field is sampled at p + offset, so a cloud moves along -offset:
// the offset integrates against the wind for clouds to travel downwind (the
// fog wisps use the same sense). wind[2] is the evolution clock: each noise
// layer slides along its own direction at its own rate per unit of it
// (kSkyLayerEvolution), so billows boil and coverage gaps open and close at
// any distance instead of the whole field translating rigidly.
// Issue #69 (candidate 29): 8-14 m/s rigid drift and a 2 m/s vertical slice
// over 500 m billows read as static kilometres away, even at 3x speed.
// Calm drift speed (m/s, F10 sky_cloud_wind_speed). Issue #115 follow-up:
// the old 10 m/s moved a cloud 20 km away about 2 degrees a minute, which the
// user read as a still sky; 25 m/s is the candidate value (look decision,
// pending the user's verdict on shadow speed in the small game view).
constexpr float kSkyCloudWindSpeed = 25.f;
// The cloud-shadow field has its own, slower drift (F10
// sky_cloud_shadow_speed; play-2 00229eb1 user verdict: the clouds that shade
// the scene should move slower while the distant sky keeps its cinematic
// pace). Same wind direction and the same evolution clock, so the dapple
// still drifts and slowly changes; it just does not race across the ~40 m
// game view. The game camera sees the horizon, not the clouds overhead that
// cast these shadows, so the two drifts never meet in one view.
constexpr float kSkyCloudShadowSpeed = 8.f;
inline void sky_step_motion(const WorldEnvironment& env, double& previous, double wind[3],
                            float wx, float wy, float force, float storm, float scale = 1.f,
                            float calm_speed = kSkyCloudWindSpeed, double* shadow = nullptr,
                            float shadow_speed = kSkyCloudShadowSpeed) noexcept {
    if (!env.valid) { previous = -1.0; return; }
    const double seconds = env.world_time * .055;
    const double dt = previous < 0.0 ? 0.0 : std::max(0.0, seconds - previous);
    previous = seconds;
    const double s = std::clamp(storm, 0.f, 1.f), k = std::max(0.f, scale);
    const double speed = (std::max(0.f, calm_speed) + 6.0 * std::clamp(force, 0.f, 1.f) + 10.0 * s) * k;
    wind[0] -= wx * speed * dt;
    wind[1] -= wy * speed * dt;
    if (shadow) {
        const double slow = (std::max(0.f, shadow_speed) + 2.0 * std::clamp(force, 0.f, 1.f) + 4.0 * s) * k;
        shadow[0] -= wx * slow * dt;
        shadow[1] -= wy * slow * dt;
    }
    // Storms boil faster. All density consumers use these same phases.
    wind[2] += (1.0 + .8 * s) * k * dt;
}

// Evolution velocity of each noise layer (world metres per unit of wind[2],
// x, y, z): the two coverage layers and the warp drift apart so local gaps
// re-form over a few minutes; the two shape layers and the detail move
// against each other (and through the altitude envelope) so 130-900 m
// billows change within tens of seconds. Proportional to wind[2] only, so a
// pure drift remains an exact world translation.
constexpr double kSkyLayerEvolution[6][3] = {
    {6, -4, 0}, {-14, 10, 0}, {10, 6, 0}, {4, -3, 14}, {-6, 5, -10}, {8, 4, 12}};

// Layer rotations and wavelengths are shared verbatim by CPU phase reduction
// and HLSL. Independent phases retain precision without resetting the field
// at the old 15 km / 1.5 km wind boundaries.
#define SKY_NOISE_LAYERS(X) \
 X(.932327, .361615, 47000.0) \
 X(.417595, -.908638, 19300.0) \
 X(.764842, .644218, 73100.0) \
 X(.891568, .452886, 2100.0) \
 X(.305817, -.952090, 3467.0) \
 X(.621610, .783327, 317.0)
struct SkyNoiseLayer { double a, b, wavelength; };
#define SKY_CPU_LAYER(a,b,s) {a,b,s},
constexpr SkyNoiseLayer kSkyNoiseLayers[] = {SKY_NOISE_LAYERS(SKY_CPU_LAYER)};
#undef SKY_CPU_LAYER
// The shape volume (layers 3 and 4) holds this many horizontal tiles of
// height, so its vertical periods (2 x 2100 m, 2 x 3467 m) exceed the 4 km
// cloud layer. Issue #115: with a cubic 2100 m tile the shape repeated 2100 m
// above itself under the same 2D coverage, so tall clouds showed a copy
// directly over them. Feature sizes are unchanged.
#define SKY_SHAPE_DEPTH_TILES 2
constexpr unsigned kSkyShapeDepthTiles = SKY_SHAPE_DEPTH_TILES;
inline void sky_noise_phases(const double wind[3], float phases[6][4]) noexcept {
    for (unsigned i = 0; i < 6; ++i) {
        const auto& l = kSkyNoiseLayers[i];
        const auto& e = kSkyLayerEvolution[i];
        const double x = wind[0] + e[0] * wind[2], y = wind[1] + e[1] * wind[2];
        // z in units of the shape volume's depth (layers 3, 4), so it wraps seamlessly.
        const double depth = i == 3 || i == 4 ? double(kSkyShapeDepthTiles) : 1.0;
        const double p[3] = {(l.a * x - l.b * y) / l.wavelength,
                             (l.b * x + l.a * y) / l.wavelength,
                             e[2] * wind[2] / (l.wavelength * depth)};
        for (unsigned j = 0; j < 3; ++j) phases[i][j] = float(p[j] - std::floor(p[j]));
        phases[i][3] = 0.f;
    }
}

// The game's light (CWorldClient::AdjustLightingWithTime, outdoor zones) turns
// a full circle a day: it shines along azimuth hour/24*360 degrees, so the
// direction toward it lies at hour*15+180, at 30 degrees elevation at 4:00 and
// 20:00, 60 at noon, and at night it climbs to 75 at midnight as the
// moonlight. The visible sun is that light: same azimuth all day, and from
// 6:30 to 18:00 exactly its direction. The light never drops below 30
// degrees, so around the stock light table's dawn (about 4:40) and dusk (about
// 19:50) the disc eases from the light down to the horizon, then follows a
// shallow arc below it so the twilight sky fades continuously.
constexpr float kSkySunrise = 4.7f, kSkySunset = 19.8f, kSkyTwilightHours = 1.8f;

inline float sky_wrap_hour(float hour) noexcept {
    float h = std::fmod(hour, 24.f);
    return h < 0.f ? h + 24.f : h;
}

inline float sky_sun_elevation_degrees(float hour, float light_elevation) noexcept {
    constexpr float pi = 3.14159265f;
    const float h = sky_wrap_hour(hour), day = kSkySunset - kSkySunrise;
    if (h >= kSkySunrise && h <= kSkySunset) {
        const float arc = 62.f * std::sin(pi * (h - kSkySunrise) / day);
        const float w = fog_smoothstep(kSkySunrise, kSkySunrise + kSkyTwilightHours, h) *
                        fog_smoothstep(kSkySunset, kSkySunset - kSkyTwilightHours, h);
        return arc + (std::clamp(light_elevation, 0.f, 89.f) - arc) * w;
    }
    const float since = h > kSkySunset ? h - kSkySunset : h + 24.f - kSkySunset;
    return -18.f * std::sin(pi * since / (24.f - day));
}

// The moon keeps to the far side of the sky: it rises as the sun sets, peaks
// at 50 degrees around midnight and sets as the sun rises, always about
// opposite the sun in azimuth (tilted 12 degrees), so the two never meet.
inline float sky_moon_elevation_degrees(float hour) noexcept {
    constexpr float pi = 3.14159265f;
    const float h = sky_wrap_hour(hour), day = kSkySunset - kSkySunrise;
    if (h >= kSkySunrise && h <= kSkySunset) return -30.f * std::sin(pi * (h - kSkySunrise) / day);
    const float since = h > kSkySunset ? h - kSkySunset : h + 24.f - kSkySunset;
    return 50.f * std::sin(pi * since / (24.f - day));
}

// Moon brightness: none while the sun is up, faint in twilight, full once the
// sun is 12 degrees below the horizon.
inline float sky_moon_visibility(float sun_elevation) noexcept {
    return fog_smoothstep(0.f, -12.f, sun_elevation);
}

struct SkyCelestial {
    float sun[3]{0, 0, 1};
    float sun_elevation = 0.f;  // degrees
    float moon[3]{0, 0, 1};
    float moon_elevation = 0.f;  // degrees
    float moon_visibility = 0.f;
};

// Sun and moon directions (toward them, Z up) from the game hour and the
// game's light direction (toward the light).
inline SkyCelestial sky_celestial(float hour, const float light[3]) noexcept {
    constexpr float pi = 3.14159265f, degrees = pi / 180.f;
    constexpr float moon_turn = (180.f - 12.f) * degrees;
    SkyCelestial c;
    float ax = light[0], ay = light[1];
    const float horizontal = std::sqrt(ax * ax + ay * ay);
    if (horizontal > 1e-3f) { ax /= horizontal; ay /= horizontal; }
    else { const float az = sky_wrap_hour(hour) / 24.f * 2.f * pi; ax = -std::cos(az); ay = -std::sin(az); }
    const float light_length = std::sqrt(horizontal * horizontal + light[2] * light[2]);
    const float light_elevation = light_length > 0.f ?
        std::atan2(light[2], horizontal) / degrees : 45.f;
    auto direction = [](float x, float y, float elevation, float* out) {
        out[0] = x * std::cos(elevation); out[1] = y * std::cos(elevation); out[2] = std::sin(elevation);
    };
    c.sun_elevation = sky_sun_elevation_degrees(hour, light_elevation);
    direction(ax, ay, c.sun_elevation * degrees, c.sun);
    c.moon_elevation = sky_moon_elevation_degrees(hour);
    const float mx = ax * std::cos(moon_turn) - ay * std::sin(moon_turn);
    const float my = ax * std::sin(moon_turn) + ay * std::cos(moon_turn);
    direction(mx, my, c.moon_elevation * degrees, c.moon);
    c.moon_visibility = sky_moon_visibility(c.sun_elevation);
    return c;
}

// Disc sizes and radiance. The sun keeps its approved disc; the moon is 0.68
// of its angular size and, at full night, far dimmer than the sun disc.
constexpr float kSkySunDiscOuterCos = .99985f, kSkySunDiscInnerCos = .99992f, kSkySunDiscRadiance = 14.f;
constexpr float kSkyMoonDiscScale = .68f, kSkyMoonDiscRadiance = .55f;
inline void sky_disc_constants(float moon_visibility, float sun_disc[4], float moon_disc[4]) noexcept {
    sun_disc[0] = kSkySunDiscOuterCos; sun_disc[1] = kSkySunDiscInnerCos;
    sun_disc[2] = kSkySunDiscRadiance; sun_disc[3] = 0.f;
    moon_disc[0] = std::cos(std::acos(kSkySunDiscOuterCos) * kSkyMoonDiscScale);
    moon_disc[1] = std::cos(std::acos(kSkySunDiscInnerCos) * kSkyMoonDiscScale);
    moon_disc[2] = kSkyMoonDiscRadiance * moon_visibility; moon_disc[3] = moon_visibility;
}

// Region skies (user decision 2026-10-01): Gipat humid and soft, Ingos cold
// and clear with crisp stars, Suslanger its own dry desert sky (harsh sun,
// dust at the horizon, warm stars) although its light table equals Gipat's.
struct SkyRegionLook {
    float coverage = .62f, density = 1.f, haze = .22f;
    float sun = 1.f, stars = 1.f, dust = 0.f;
    float star_tint[3]{.85f, .92f, 1.f};
};
inline SkyRegionLook sky_region_look(int region) noexcept {
    SkyRegionLook look;
    if (region == fog_region_ingos) {
        look.coverage = .55f; look.density = .9f; look.haze = .06f; look.stars = 1.35f;
        look.star_tint[0] = .8f; look.star_tint[1] = .9f; look.star_tint[2] = 1.05f;
    } else if (region == fog_region_suslanger) {
        look.coverage = .45f; look.density = .85f; look.haze = .3f; look.sun = 1.25f; look.dust = 1.f;
        look.star_tint[0] = 1.05f; look.star_tint[1] = .95f; look.star_tint[2] = .8f;
    } else {
        look.stars = .8f;  // Gipat humid: softer stars
    }
    return look;
}

// The weather's current kind is also the emitted particle kind. Entering
// precipitation sets current == next immediately; its power then rises from
// zero. Close during the particle-free start of that cosine power curve,
// and reopen only at its particle-free end, without an extra hours-long lag.
inline float sky_overcast_target(const WorldEnvironment& e) noexcept {
    if (e.cave) return 0.f;
    const float p = std::clamp(e.weather_progress, 0.f, 1.f);
    if (e.weather_current == 0) return 0.f;
    // First of 64 rain power steps starts at p ~= .056. Both ends
    // therefore finish their cloud transition before a rain step is emitted.
    return e.weather_next == 0 ? 1.f - fog_smoothstep(.95f, 1.f, p)
                              : fog_smoothstep(0.f, .05f, p);
}

// Deterministic, continuous day variation in [-1, 1]. A midnight must not
// replace the coverage threshold (and erase whole low-coverage cloud banks).
inline std::uint32_t sky_hash(std::uint32_t h) noexcept {
    h = h * 0x2545f491u + 0x9e3779b9u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
inline float sky_hash_signed(std::uint32_t key) noexcept {
    return static_cast<float>(sky_hash(key) >> 8) / 8388608.f - 1.f;
}
// Smooth 1D value noise in [-1, 1] over x >= 0 (one new value per unit).
inline float sky_smooth_noise(double x, std::uint32_t seed) noexcept {
    if (!(x >= 0.0)) return 0.f;
    const auto index = static_cast<std::uint32_t>(static_cast<std::int64_t>(x));
    const float f = static_cast<float>(x - std::floor(x));
    const float u = f * f * f * (f * (f * 6.f - 15.f) + 10.f);
    const float a = sky_hash_signed(index ^ seed);
    return a + (sky_hash_signed((index + 1) ^ seed) - a) * u;
}
inline float sky_day_variation(double game_hours) noexcept {
    return sky_smooth_noise(game_hours / 24.0, 0u);
}
// Fair-weather spells over game seconds: the sky clears out and builds up
// again every few minutes (a new value every 200 game seconds).
inline float sky_spell_variation(double game_seconds) noexcept {
    return sky_smooth_noise(game_seconds / 200.0, 0x51ed270bu);
}

// Weather closure of the sky over game time. The game switches its weather
// kind within about a second (CWeather transition of 20 segments), and its
// rain spells are short: the server rolls 1 in 31 to end the rain every 64
// segments (3.5 game s) after a 10-roll delay, so rain lasts from about 35 s,
// typically 2-3 game minutes (engine reference, level 3). A 90 s build left
// the deck open for most of a spell and then cleared it under the fading
// rain, so the deck now closes in 20 game seconds, is held while the
// renderer's own ramped rain or snow lasts (sky_closure_target), and clears
// over 120 s after it. Pause holds it, fast speed shortens it; the first sky
// of a scene (elapsed < 0) starts at the target.
constexpr double kSkyCloseSeconds = 20.0, kSkyClearSeconds = 120.0;
// The deck stays closed while the ramped precipitation that drives the rain
// fog, wetness and particles (fog_ramp_precipitation) still shows. The ramp
// only holds a deck the game's rain closed; it never closes one. level < 0
// (the first sky of a scene) takes the game's weather alone. Runtime
// (026b5e6, save1133 clear dusk): the main menu's rain left the ramp at 1
// (it updates after the sky in a frame and may ramp across a load), so a
// loaded clear world snapped its deck shut and cleared it over two minutes,
// greying the horizon and hiding the water reflections.
inline float sky_closure_target(float game_target, float ramped_precipitation, float level) noexcept {
    const float hold = level < 0.f ? 0.f : std::min(level, fog_smoothstep(.02f, .2f, ramped_precipitation));
    return std::max(std::clamp(game_target, 0.f, 1.f), hold);
}
inline float sky_step_closure(float level, float target, double elapsed_seconds) noexcept {
    if (level < 0.f || elapsed_seconds < 0.0) return target;
    const double rate = target > level ? 1.0 / kSkyCloseSeconds : 1.0 / kSkyClearSeconds;
    const float step = static_cast<float>(elapsed_seconds * rate);
    return level + std::clamp(target - level, -step, step);
}

// Distant lightning inside the storm deck (renderer-owned: the game signals
// its own ground strikes only through world scripts, not to the sky). One
// candidate flash per 2.6 game-second slot, taken with a chance that follows
// the storm level; each lights the deck with a short 3-pulse flicker at a
// hashed point 3-14 km from the view, inside the lower cloud layer.
// Returns the intensity (0 none) and the flash position relative to the eye.
// Three decaying pulses over game seconds since the flash began: a bright
// leader, a dimmer return, a faint tail (0 outside 0 .. kSkyFlashSeconds).
constexpr float kSkyFlashSeconds = .55f;
inline float sky_flash_pulses(float t) noexcept {
    if (!(t >= 0.f) || t > kSkyFlashSeconds) return 0.f;
    auto pulse = [t](float at, float width) { const float x = (t - at) / width; return std::exp(-x * x); };
    return pulse(.03f, .03f) + .6f * pulse(.17f, .04f) + .35f * pulse(.36f, .06f);
}
inline float sky_lightning(double game_seconds, float storm, float offset[3]) noexcept {
    offset[0] = offset[1] = 0.f; offset[2] = 1500.f;
    const float s = std::clamp(storm, 0.f, 1.f);
    if (s <= 0.f || !(game_seconds >= 0.0)) return 0.f;
    constexpr double slot_seconds = 2.6;
    const auto slot = static_cast<std::uint32_t>(static_cast<std::int64_t>(game_seconds / slot_seconds));
    const float u = (sky_hash_signed(slot * 4u) + 1.f) * .5f;
    if (u > .45f * s * s) return 0.f;
    const float start = (sky_hash_signed(slot * 4u + 1u) + 1.f) * .5f * 1.8f;
    const float t = static_cast<float>(game_seconds - slot * slot_seconds) - start;
    if (t < 0.f || t > .55f) return 0.f;
    const float intensity = sky_flash_pulses(t);
    const float azimuth = (sky_hash_signed(slot * 4u + 2u) + 1.f) * 3.14159265f;
    const float h = (sky_hash_signed(slot * 4u + 3u) + 1.f) * .5f;
    const float distance = 3000.f + 11000.f * h;
    offset[0] = distance * std::cos(azimuth); offset[1] = distance * std::sin(azimuth);
    offset[2] = 1400.f + 1200.f * h;  // inside the lower deck (800 m base)
    return intensity * s;
}

// The game's rain strike (level 1: the script CWorldInterface::InvokeLightning
// formats, game.exe string at file offset 0x390394) creates
// CreatePointLight(1,x,y,z,80,255,255,255) at the bolt's middle, then two
// bolts, and deletes the light two script Sleeps later; the F10 debug strike
// runs the same script. A new white radius-80 world light is that strike: its
// flash lights the deck above it (multiple scattering in the lower layer), so
// thick storm cloud glows around the bolt and thin cloud barely does.
constexpr float kSkyStrikeLightRadius = 80.f, kSkyStrikeFlashAltitude = 1400.f, kSkyStrikeFlash = .6f;
template<class Light> bool sky_strike_light(const Light& light) noexcept {
    return !light.attached && light.radius == kSkyStrikeLightRadius &&
           light.color[0] == light.color[1] && light.color[1] == light.color[2];
}

// Smooth fair-weather coverage across region or scripted clock changes.
// Pause holds it; the first sky and a backwards load seed the new scene.
inline float sky_step_coverage(float level, float target, double elapsed_seconds) noexcept {
    if (level < 0.f || elapsed_seconds < 0.0) return target;
    return target + (level - target) * static_cast<float>(std::exp(-elapsed_seconds / 30.0));
}

// Cloud layer altitudes (world metres above the game's zero). The volume
// spans the low stratus base to the cumulonimbus tops; each cloud type uses
// its own band of it (see skyCloudProfile). Cirrus is a thin 2D layer far
// above the volume and is composited behind it.
constexpr float kSkyCloudBase = 800.f, kSkyCloudTop = 4800.f, kSkyCirrusAltitude = 8500.f;
// The upper wind veers from the cloud wind and blows faster.
constexpr float kSkyCirrusVeerDegrees = 25.f, kSkyCirrusSpeed = 2.5f;
// Layered wind (issue #115 follow-up, user: "movement should be layered,
// realistic shadow like"): each cloud population drifts as a whole at its
// own altitude's wind, faster and veered with height (shear), rather than
// one rigid sheet. The low volume (stratus, cumulus, nimbus; 0.8-3 km) takes
// the cloud wind itself; a thin mid-level altocumulus sheet (3.4-3.9 km)
// takes it 1.4x faster, veered 12 degrees; cirrus (8.5 km) 2.5x, 25 degrees.
// Every layer's offset is the low offset scaled and rotated, so a turning
// game wind turns all layers together and no extra clock exists.
constexpr float kSkyMidVeerDegrees = 12.f, kSkyMidSpeed = 1.4f;
#define SKY_MID_COVERAGE_WAVELENGTH 23000.0
#define SKY_MID_SHAPE_WAVELENGTH 1050.0
constexpr double kSkyMidCoverageWavelength = SKY_MID_COVERAGE_WAVELENGTH;
constexpr double kSkyMidShapeWavelength = SKY_MID_SHAPE_WAVELENGTH;
// Mid-sheet band over the volume's height fraction (0 = 800 m, 1 = 4800 m).
#define SKY_MID_BAND .66, .69, .73, .77
// Cirrus streak wavelengths (along, across the upper wind) of its two taps.
#define SKY_CIRRUS_SCALE 36000.0, 9000.0, 14000.0, 1400.0
constexpr double kSkyCirrusScale[4] = {SKY_CIRRUS_SCALE};

// Cloud types follow a scalar type field (Nubis convention): 0 stratus,
// .5 cumulus, 1 cumulonimbus. A slowly drifting large-scale noise n in [0, 1]
// picks t = base + spread * (2n - 1) locally, so the same sky holds several
// types; the weather moves the whole range. Fair days are scattered cumulus
// (gusty spells build taller convection), a closing sky lays a low stratus
// deck, rain and snow bring dark towering nimbus.
struct SkyCloudWeather {
    float type_base = .5f, type_spread = .22f;
    float storm = 0.f;   // precipitation 0..1: nimbus extinction
    float cirrus = 0.f;  // high cirrus amount 0..1
};
// Precipitation look of the deck. The closure only rises for rain or snow
// (sky_overcast_target), so a closing deck is a storm deck: it darkens into
// nimbus as it closes, before the first drops, and holds through the rain
// until the closure clears. The ramped game rain (a quarter game hour, longer
// than the 90 s closure) only adds where it is ahead. Driving the type by that
// ramp alone took a closed stratus deck through scattered cumulus (type .5)
// half way to nimbus, in rain, and back again as the rain eased.
// Storm front (issue #115/#145, user-approved design): the deck is a band of
// the sky between a leading edge and a trailing edge on the wind axis,
// measured in metres upwind of the eye, so storms move in from the upwind
// horizon and clear from the upwind side instead of the whole sky morphing.
// The game switches its weather within a second; on that switch small
// scattered clouds form overhead (pre, about 15 s) with the first light rain,
// and the leading edge sets off 12 km upwind at 500 m/s (a formation front:
// the clouds inside still drift with the storm wind), passing overhead after
// about 24 s as the rain intensifies (the weather side gates its ramp by
// sky_front_arrival). Clearing: the trailing edge starts 40 km upwind and
// moves downwind at 180 m/s, clearing overhead after about 3.7 min. A
// reversed weather reverses the edge that has not passed yet.
constexpr float kSkyFrontStart = 12000.f, kSkyFrontSpeed = 500.f, kSkyRearStart = 40000.f, kSkyRearSpeed = 180.f;
constexpr float kSkyFrontGone = -80000.f, kSkyFrontOpen = 1.0e6f, kSkyFrontBuild = 4000.f;
constexpr float kSkyPreFormSeconds = 15.f, kSkyPreClearSeconds = 60.f;
struct SkyFront { float lead = kSkyFrontGone, rear = kSkyFrontGone, pre = 0.f; };
inline bool sky_front_present(const SkyFront& f) noexcept { return f.rear - f.lead > kSkyFrontBuild && f.rear > kSkyFrontGone; }
inline void sky_step_front(SkyFront& f, bool storm, double elapsed) noexcept {
    if (elapsed < 0.0) {  // load or jump: the scene starts in its own weather
        f.lead = kSkyFrontGone; f.rear = storm ? kSkyFrontOpen : kSkyFrontGone; f.pre = storm ? 1.f : 0.f;
        return;
    }
    const float dt = static_cast<float>(elapsed);
    if (storm) {
        if (!sky_front_present(f)) { f.lead = kSkyFrontStart; f.rear = kSkyFrontOpen; }
        f.lead = std::max(kSkyFrontGone, f.lead - kSkyFrontSpeed * dt);
        if (f.rear < kSkyFrontOpen) { f.rear += kSkyRearSpeed * dt; if (f.rear > 2.f * kSkyRearStart) f.rear = kSkyFrontOpen; }
        f.pre = std::min(1.f, f.pre + dt / kSkyPreFormSeconds);
    } else {
        if (sky_front_present(f) && f.rear >= kSkyFrontOpen) f.rear = kSkyRearStart;
        if (f.rear < kSkyFrontOpen) f.rear = std::max(kSkyFrontGone, f.rear - kSkyRearSpeed * dt);
        // A leading edge that has not reached the eye retreats; one past it moves off downwind.
        f.lead = f.lead > 0.f ? f.lead + kSkyFrontSpeed * dt : std::max(kSkyFrontGone, f.lead - kSkyFrontSpeed * dt);
        f.pre = std::max(0.f, f.pre - dt / kSkyPreClearSeconds);
    }
}
// The deck amount at `along` metres upwind of the eye (the shader's skyDeckAt).
inline float sky_front_deck(const SkyFront& f, float along) noexcept {
    auto smooth = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); };
    return smooth(f.lead, f.lead + kSkyFrontBuild, along) * (1.f - smooth(f.rear - kSkyFrontBuild, f.rear, along));
}
inline float sky_deck_storm(float closed, float ramped_storm, float kind) noexcept {
    return std::max(std::clamp(ramped_storm, 0.f, 1.f), std::clamp(closed, 0.f, 1.f) * std::clamp(kind, 0.f, 1.f));
}
inline SkyCloudWeather sky_cloud_weather(float closed, float storm, float windy, float day) noexcept {
    const float c = std::clamp(closed, 0.f, 1.f), s = std::clamp(storm, 0.f, 1.f), w = std::clamp(windy, 0.f, 1.f);
    SkyCloudWeather out;
    // Fair skies centre on cumulus with less stratus weight (cloud-shape:
    // type .28 put 44% stratus into a column, whose blended band pulled the
    // cumulus top down and whose sheet fill flattened it into wide lens
    // pancakes). Fair type now spans about .39-.73: at most 22% stratus.
    float base = .56f + .1f * w;
    base += (.06f - base) * c;
    base += (.92f - base) * s;
    out.type_base = base;
    out.type_spread = .17f + .06f * w - .03f * c * (1.f - s) + .17f * s;
    out.storm = s;
    out.cirrus = std::clamp(.45f + .25f * std::clamp(day, -1.f, 1.f) + .2f * w, 0.f, 1.f) * (1.f - c);
    return out;
}
// Mid sheet phases from the integrated low offset: coverage (xy, in its own
// rotated frame) and shape (xyz, the shape volume is kSkyShapeDepthTiles tall;
// z slides slowly with the evolution clock, 4 m per unit).
inline void sky_mid_phases(const double wind[3], float coverage[2], float shape[3]) noexcept {
    constexpr double veer = kSkyMidVeerDegrees * 3.14159265358979 / 180.0;
    const double cv = std::cos(veer), sv = std::sin(veer);
    const double ox = kSkyMidSpeed * (cv * wind[0] - sv * wind[1]);
    const double oy = kSkyMidSpeed * (sv * wind[0] + cv * wind[1]);
    // Same rotations as the HLSL (skyMidDensity).
    const double c[2] = {(.573576 * ox - .819152 * oy) / kSkyMidCoverageWavelength,
                         (.819152 * ox + .573576 * oy) / kSkyMidCoverageWavelength};
    const double s[3] = {(.939693 * ox - .342020 * oy) / kSkyMidShapeWavelength,
                         (.342020 * ox + .939693 * oy) / kSkyMidShapeWavelength,
                         4.0 * wind[2] / (kSkyMidShapeWavelength * kSkyShapeDepthTiles)};
    for (int i = 0; i < 2; ++i) coverage[i] = static_cast<float>(c[i] - std::floor(c[i]));
    for (int i = 0; i < 3; ++i) shape[i] = static_cast<float>(s[i] - std::floor(s[i]));
}
// Upper-wind direction and its reduced streak phases from the integrated
// cloud wind offset (world metres); double precision before the reduction.
inline void sky_cirrus_motion(const double wind[3], float wx, float wy, float direction[2], float phases[4]) noexcept {
    constexpr double veer = kSkyCirrusVeerDegrees * 3.14159265358979 / 180.0;
    const double cv = std::cos(veer), sv = std::sin(veer);
    double dx = cv * wx - sv * wy, dy = sv * wx + cv * wy;
    const double l = std::sqrt(dx * dx + dy * dy);
    if (l > 1e-6) { dx /= l; dy /= l; } else { dx = 1.0; dy = 0.0; }
    const double ox = kSkyCirrusSpeed * (cv * wind[0] - sv * wind[1]);
    const double oy = kSkyCirrusSpeed * (sv * wind[0] + cv * wind[1]);
    const double along = dx * ox + dy * oy, across = -dy * ox + dx * oy;
    // Same sense as the volume's noise phases: the field is sampled at p + offset.
    const double p[4] = {along / kSkyCirrusScale[0], across / kSkyCirrusScale[1],
                         along / kSkyCirrusScale[2], across / kSkyCirrusScale[3]};
    for (int i = 0; i < 4; ++i) phases[i] = static_cast<float>(p[i] - std::floor(p[i]));
    direction[0] = static_cast<float>(dx); direction[1] = static_cast<float>(dy);
}

// Transmittance of the light through the same atmosphere the sky LUT uses.
inline void sky_light_transmittance(const float d[3], float haze, float out[3], float altitude = 20.f) noexcept {
    const double Rg = 6360e3, Rt = 6420e3, z0 = Rg + altitude;
    const double dz = d[2];
    // A below-horizon sun can still reach elevated clouds. Test the actual
    // planet intersection, rather than sampling through it or clamping the ray.
    if (dz < 0 && z0 * z0 * (1 - dz * dz) < Rg * Rg) {
        out[0] = out[1] = out[2] = 0.f;
        return;
    }
    const double b = z0 * dz, c = z0 * z0 - Rt * Rt, len = -b + std::sqrt(b * b - c);
    const double bR[3] = {5.802e-6, 13.558e-6, 33.1e-6}, bO[3] = {0.65e-6, 1.881e-6, 0.085e-6};
    double od[3] = {};
    for (int i = 0; i < 48; ++i) {
        const double t = (i + .5) / 48 * len, x = d[0] * t, y = d[1] * t, z = z0 + dz * t;
        const double alt = std::sqrt(x * x + y * y + z * z) - Rg, dt = len / 48;
        if (alt < 0) { od[0] = od[1] = od[2] = 1e3; break; }
        const double r = std::exp(-alt / 8000), m = std::exp(-alt / 1200) * (1 + haze * 6),
                     o = std::max(0.0, 1 - std::fabs(alt - 25000) / 15000);
        for (int k = 0; k < 3; ++k) od[k] += (bR[k] * r + 4.44e-6 * m + bO[k] * o) * dt;
    }
    for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(std::exp(-od[k]));
}

// Direct irradiance belongs to the illuminated layer, not the observer on
// the ground. Precompute its vertical endpoints on the CPU; the march only
// interpolates them and keeps its existing light-density sample count.
inline void sky_cloud_irradiance(const float direction[3], float haze, const float color[3],
                                 float gain, float altitude, float out[3]) noexcept {
    sky_light_transmittance(direction, haze, out, altitude);
    for (int k = 0; k < 3; ++k) out[k] *= color[k] * gain;
}

struct SkyFrameGpu {
    float clip_to_world[16]{};
    float previous_world_to_clip[16]{};
    float eye[4]{};             // camera position
    float previous[4]{};        // w: probe/cloud history valid
    float sun[4]{};             // visual sun direction, w: sun disc weight
    float moon[4]{};            // moon direction, w: night weight
    float light_radiance[4]{};  // ground-level sun radiance for the disc, w: exposure
    float cloud_light[4]{};     // cloud light direction
    float lut_sun[4]{};         // sky LUT scale for the sun, w: star brightness
    float lut_moon[4]{};        // sky LUT scale for the moon, w: astral glow
    float light[4]{};           // game light direction (shadows), w: cloud shadow strength
    float cloud[4]{};           // bottom, top, coverage, density
    float weather[4]{};         // haze, overcast, wetness, cloud history weight
    float noise_phase[6][4]{};  // independently reduced wind phases for the six layers
    float shadow[4]{};          // shadow map origin x, y (min corner), extent, overcast floor
    float horizon[4]{};         // display haze colour of the fog, w: horizon blend
    float game[4]{};            // game sky colour (linear), w: night airglow
    float stars[4]{};           // star tint, w: horizon dust
    float sun_disc[4]{};        // disc outer cos, inner cos, radiance
    float moon_disc[4]{};       // disc outer cos, inner cos, radiance, w: moon visibility
    std::uint32_t size[4]{};    // cloud width, height, screen width, height
    std::uint32_t control[4]{}; // frame, steps, light steps, probe width
    std::uint32_t reconstruct[4]{}; // reconstruction width, height, this frame's 2x2 sub-pixel x, y
    float types[4]{};           // cloud type base, spread, storm, deck closure
    float cirrus[4]{};          // upper wind direction x, y, amount, altitude
    float cirrus_phase[4]{};    // reduced streak phases (along, across) of the two taps
    float cloud_bottom[4]{};    // direct irradiance at the volume base (linear)
    float cloud_top[4]{};       // direct irradiance at the volume top (linear)
    float cirrus_light[4]{};    // ice-layer irradiance (linear), w: sun rather than moon
    float storm_light[4]{};     // low sun under the storm deck (linear irradiance), w: deck edge radius (m)
    float flash[4]{};           // lightning position (world), w: intensity
    float rain[4]{};            // rain-shaft amount, slant direction x, y, w: deck edge strength
    float mid[4]{};             // mid sheet coverage phase u, v, amount, density
    float mid_shape[4]{};       // mid sheet shape phase x, y, z
    float deck_types[4]{};      // the storm deck's cloud type base, spread, storm, 1 (types holds the fair sky's)
    float deck_cloud[4]{};      // deck coverage, density, leading and trailing edge (m upwind of the eye)
    float deck_axis[4]{};       // upwind direction x, y, edge build width (m), pre-storm overhead cover
};
static_assert(sizeof(SkyFrameGpu) == 768, "sky constants must match HLSL");

// Temporal cloud reconstruction: each frame marches one sub-pixel of every
// 2x2 block of the reconstruction target, so four consecutive frames march
// every reconstruction pixel once (diagonal order spreads the refresh).
inline void sky_reconstruct_offset(std::uint32_t frame, std::uint32_t out[2]) noexcept {
    static constexpr std::uint32_t order[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
    out[0] = order[frame & 3u][0];out[1] = order[frame & 3u][1];
}

constexpr unsigned kSkyLutWidth = 192, kSkyLutHeight = 108;

#define SKY_STRING_INNER(x) #x
#define SKY_STRING(x) SKY_STRING_INNER(x)
#define SKY_STRING_LIST_INNER(...) #__VA_ARGS__
#define SKY_STRING_LIST(...) SKY_STRING_LIST_INNER(__VA_ARGS__)
#define SKY_HLSL_LAYER(a,b,s) "float3(" SKY_STRING(a) "," SKY_STRING(b) "," SKY_STRING(s) "),"
constexpr char kSkyShader[] =
"static const float3 skyNoiseLayers[6]={" SKY_NOISE_LAYERS(SKY_HLSL_LAYER) "};\n"
"static const float4 skyCirrusScale=float4(" SKY_STRING_LIST(SKY_CIRRUS_SCALE) ");\n"
"static const float4 skyMidBand=float4(" SKY_STRING_LIST(SKY_MID_BAND) ");\n"
"static const float2 skyMidWavelength=float2(" SKY_STRING(SKY_MID_COVERAGE_WAVELENGTH) "," SKY_STRING(SKY_MID_SHAPE_WAVELENGTH) ");\n"
"static const int SKY_SHAPE_DEPTH=" SKY_STRING(SKY_SHAPE_DEPTH_TILES) ";\n"
R"(
cbuffer SkyFrame:register(b0){row_major float4x4 skyClipToWorld;row_major float4x4 skyPreviousWorldToClip;
 float4 skyEye;float4 skyPrevious;float4 skySun;float4 skyMoon;float4 skyLightRadiance;float4 skyCloudLight;
 float4 skyLutSun;float4 skyLutMoon;float4 skyLight;float4 skyCloud;float4 skyWeather;float4 skyNoisePhase[6];
 float4 skyShadow;float4 skyHorizon;float4 skyGame;float4 skyStars;float4 skySunDisc;float4 skyMoonDisc;uint4 skySize;uint4 skyControl;uint4 skyReconstruct;
 float4 skyTypes;float4 skyCirrus;float4 skyCirrusPhase;
 float4 skyCloudBottom;float4 skyCloudTop;float4 skyCirrusLight;float4 skyStormLight;float4 skyFlash;float4 skyRain;
 float4 skyMid;float4 skyMidShape;float4 skyDeckTypes;float4 skyDeckCloud;float4 skyDeckAxis;};
static const float SKY_PI=3.14159265;
static const float2 SKY_LUT=float2(192,108);
Texture3D<float> skyShapeNoise:register(t0);
Texture3D<float> skyDetailNoise:register(t1);
Texture2D<float2> skyWeatherMap:register(t2);
Texture2D<float4> skyLut:register(t3);
Texture2D<float4> skyClouds:register(t4);
Texture2D<float4> skyHistory:register(t5);
SamplerState skyWrap:register(s0);
SamplerState skyClamp:register(s1);
RWTexture3D<float> skyNoiseOut:register(u0);
RWTexture2D<float2> skyWeatherOut:register(u1);
RWTexture2D<float4> skyColorOut:register(u2);
RWTexture2D<float> skyShadowOut:register(u3);
Texture2D<float> skyShadowRaw:register(t6);

// ------------------------------------------------------------ noise (tiling)
uint skyHash3(int3 p){uint h=(uint)p.x*73856093u^(uint)p.y*19349663u^(uint)p.z*83492791u;h^=h>>13;h*=0x5bd1e995u;h^=h>>15;return h;}
float3 skyGrad3(int3 p){uint h=skyHash3(p);return normalize(float3(h&1023u,(h>>10)&1023u,(h>>20)&1023u)/511.5-1+1e-4);}
float skyPerlin3(float3 p,int3 period){
 int3 i=(int3)floor(p);float3 f=frac(p);float3 u=f*f*f*(f*(f*6-15)+10);float r=0;
 [loop]for(int k=0;k<8;++k){int3 o=int3(k&1,(k>>1)&1,k>>2);int3 c=(i+o)%period;c+=c<0?period:0;
  float w=(o.x?u.x:1-u.x)*(o.y?u.y:1-u.y)*(o.z?u.z:1-u.z);r+=w*dot(skyGrad3(c),f-o);}
 return r*.5+.5;
}
float skyWorley3(float3 p,int3 period){
 int3 i=(int3)floor(p);float3 f=frac(p);float d=1;
 [loop]for(int k=0;k<27;++k){int3 o=int3(k%3,(k/3)%3,k/9)-1;int3 c=(i+o)%period;c+=c<0?period:0;
  uint h=skyHash3(c+7);float3 j=float3(h&255u,(h>>8)&255u,(h>>16)&255u)/255.0;d=min(d,length(o+j-f));}
 return 1-saturate(d);
}
float skyRemap(float v,float a,float b,float c,float d){return c+(v-a)/(b-a)*(d-c);}
// 128 x 128 texels per tile horizontally, SKY_SHAPE_DEPTH tiles (128 texels
// each) of height: p spans [0,1) x [0,1) x [0,SKY_SHAPE_DEPTH).
[numthreads(4,4,4)]void skyShapeCS(uint3 id:SV_DispatchThreadID){
 float3 p=(id+.5)/128.0;
 int3 e=int3(1,1,SKY_SHAPE_DEPTH);
 // Cube-axis rotations preserve seamless wrapping of the stored volume (each
 // lattice period follows its permuted axis extent), while independent
 // offsets decorrelate its generated octaves.
 float pf=skyPerlin3(p*4+float3(.17,.43,.71),e*4)*.625+skyPerlin3(p.yzx*8+float3(2.31,5.17,1.73),e.yzx*8)*.25+skyPerlin3(p.zxy*16+float3(7.13,3.61,11.29),e.zxy*16)*.125;
 float wf=skyWorley3(p*4+float3(1.37,.59,2.11),e*4)*.625+skyWorley3(p.zxy*8+float3(3.71,1.23,5.47),e.zxy*8)*.25+skyWorley3(p.yzx*16+float3(9.19,7.43,2.89),e.yzx*16)*.125;
 skyNoiseOut[id]=saturate(skyRemap(pf,wf-1,1,0,1));
}
[numthreads(4,4,4)]void skyDetailCS(uint3 id:SV_DispatchThreadID){
 float3 p=(id+.5)/32.0;
 skyNoiseOut[id]=skyWorley3(p*2+float3(.31,.73,1.17),2)*.625+skyWorley3(p.yzx*4+float3(1.59,2.37,.43),4)*.25+skyWorley3(p.zxy*8+float3(3.13,5.71,1.89),8)*.125;
}
[numthreads(8,8,1)]void skyWeatherCS(uint3 id:SV_DispatchThreadID){
 float3 p=float3((id.xy+.5)/512.0,.37);
 float c=0,a=.5;int period=4;float3 q=p;
 [loop]for(int o=0;o<5;++o){c+=a*skyPerlin3(q*period,period);a*=.5;period*=2;q=float3(q.y,-q.x,q.z)+float3(.37,.61,.19);}
 c=saturate((c-.5)*2.2+.5)*.7+skyWorley3(p*6,6)*.3;
 skyWeatherOut[id.xy]=float2(c,skyPerlin3(p*3+11.3,3));
}

// ------------------------------------------------------------ atmosphere
static const float SKY_RG=6360e3,SKY_RT=6420e3;
static const float3 SKY_BETA_R=float3(5.802e-6,13.558e-6,33.1e-6);
static const float SKY_BETA_MS=3.996e-6,SKY_BETA_ME=4.44e-6;
static const float3 SKY_BETA_O=float3(.65e-6,1.881e-6,.085e-6);
float2 skyRaySphere(float3 o,float3 d,float r){float b=dot(o,d),c=dot(o,o)-r*r,h=b*b-c;if(h<0)return float2(-1,-1);h=sqrt(h);return float2(-b-h,-b+h);}
float3 skyExtinction(float alt,float hazeMul){return SKY_BETA_R*exp(-alt/8000)+SKY_BETA_ME*exp(-alt/1200)*hazeMul+SKY_BETA_O*max(0,1-abs(alt-25000)/15000);}
float skyPhaseR(float c){return 3/(16*SKY_PI)*(1+c*c);}
float skyPhaseHG(float c,float g){float g2=g*g;return (1-g2)/(4*SKY_PI*pow(max(1e-4,1+g2-2*g*c),1.5));}
float3 skyOpticalToLight(float3 p,float3 s,float hazeMul){
 float len=max(0,skyRaySphere(p,s,SKY_RT).y);float3 od=0;float dt=len/8;
 [unroll]for(int i=0;i<8;++i)od+=skyExtinction(length(p+s*(i+.5)*dt)-SKY_RG,hazeMul)*dt;
 return od;
}
float skyLutV(float elevation){return .5+.5*sign(elevation)*sqrt(abs(elevation)/(SKY_PI/2));}
float skyLutElevation(float v){float x=v*2-1;return sign(x)*x*x*(SKY_PI/2);}
float3 skyLightScatter(float3 p,float3 s,float alt,float pr,float pm,float hazeMul){
 float visible=skyRaySphere(p,s,SKY_RG).x>0?0:1;
 float3 transmitted=exp(-skyOpticalToLight(p,s,hazeMul))*visible;
 float r=exp(-alt/8000),m=exp(-alt/1200)*hazeMul;
 // single scattering plus a cheap isotropic multiple-scattering lift
 return (SKY_BETA_R*r*pr+SKY_BETA_MS*m*pm)*transmitted+(SKY_BETA_R*r+SKY_BETA_MS*m)*.08*saturate(s.z*4+.3);
}
// Sky-view LUT: u = absolute azimuth, v = non-linear elevation; sun and moon.
[numthreads(8,8,1)]void skyLutCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=(uint2)SKY_LUT))return;
 float2 uv=(id.xy+.5)/SKY_LUT;
 float az=uv.x*2*SKY_PI,el=skyLutElevation(uv.y);
 float3 d=float3(cos(az)*cos(el),sin(az)*cos(el),sin(el));
 float3 o=float3(0,0,SKY_RG+max(skyEye.z,1)+2);
 float2 ta=skyRaySphere(o,d,SKY_RT),tg=skyRaySphere(o,d,SKY_RG);
 float len=tg.x>0?tg.x:ta.y;
 float hazeMul=1+skyWeather.x*6;
 float cs=dot(d,skySun.xyz),cm=dot(d,skyMoon.xyz);
 float prs=skyPhaseR(cs),pms=skyPhaseHG(cs,.8),prm=skyPhaseR(cm),pmm=skyPhaseHG(cm,.8);
 bool sun=any(skyLutSun.rgb>0),moon=any(skyLutMoon.rgb>0);
 float3 L=0,od=0;
 [loop]for(int i=0;i<32;++i){
  float t0=len*pow(i/32.0,2),t1=len*pow((i+1)/32.0,2),dt=t1-t0;
  float3 p=o+d*(t0+dt*.5);float alt=length(p)-SKY_RG;
  float3 e=skyExtinction(alt,hazeMul);
  float3 scatter=0;
  [branch]if(sun)scatter+=skyLightScatter(p,skySun.xyz,alt,prs,pms,hazeMul)*skyLutSun.rgb;
  [branch]if(moon)scatter+=skyLightScatter(p,skyMoon.xyz,alt,prm,pmm,hazeMul)*skyLutMoon.rgb;
  L+=exp(-od-e*dt*.5)*scatter*dt;od+=e*dt;
 }
 skyColorOut[id.xy]=float4(L,1);
}
float3 skyBaseRadiance(float3 d){
 float az=atan2(d.y,d.x+1e-7)*(.5/SKY_PI);
 float v=clamp(skyLutV(asin(clamp(d.z,-1,1))),.5/SKY_LUT.y,1-.5/SKY_LUT.y);
 float3 L=skyLut.SampleLevel(skyWrap,float2(az,v),0).rgb;
 // Stylized grade: pull the horizon toward the game's authored sky colour.
 float horizon=pow(1-saturate(abs(d.z)),4);
 L=lerp(L,skyGame.rgb*dot(L,float3(.2126,.7152,.0722))*2.2,horizon*.35);
 // Overcast: a bright grey dome. Under a storm deck with an edge (skyRain.w)
 // the low sky beyond that edge keeps its own colour: the bright horizon gap.
 float3 grey=dot(L,float3(.3,.5,.2))*float3(.95,.97,1)*1.1+.03*(1-skyMoon.w);
 // The low sky takes the overcast grey too (lighter at the horizon through
 // the distant deck's fade): an ungreyed gap under the deck read as a teal
 // stripe (runtime, sky_weather_test 1).
 L=lerp(L,grey,skyWeather.y);
 // Night: the stock night sky is deep blue, never black.
 return L+skyGame.rgb*skyGame.w*(1-.4*saturate(d.z));
}

)"
R"(
// ------------------------------------------------------------ clouds
float3 skyNoiseCoord(float3 p,int layer){
 float3 l=skyNoiseLayers[layer];
 return float3(l.x*p.x-l.y*p.y,l.y*p.x+l.x*p.y,p.z)/l.z+skyNoisePhase[layer].xyz;
}
// Shape volume coordinate: the volume is SKY_SHAPE_DEPTH tiles tall, so z
// (and its phase, reduced on the CPU in the same unit) spans that depth.
float3 skyShapeCoord(float3 p,int layer){
 float3 l=skyNoiseLayers[layer];
 return float3(l.x*p.x-l.y*p.y,l.y*p.x+l.x*p.y,p.z/SKY_SHAPE_DEPTH)/l.z+skyNoisePhase[layer].xyz;
}
float2 skyWeatherAt(float2 xy){
 // Broad warp bends the smaller coverage structures without aligning them
 // with the shape lattice. Three taps, no larger weather allocation.
 float2 warp=(skyWeatherMap.SampleLevel(skyWrap,skyNoiseCoord(float3(xy,0),2).xy+float2(.17,.63),0)-.5)*9000;
 float2 a=skyWeatherMap.SampleLevel(skyWrap,skyNoiseCoord(float3(xy+warp,0),0).xy+float2(.31,.07),0);
 float2 b=skyWeatherMap.SampleLevel(skyWrap,skyNoiseCoord(float3(xy+warp,0),1).xy+float2(.73,.49),0);
 float2 w=a*.6+b*.4;
 // Recover contrast lost by blending: distinct cores and clear gaps.
 return float2(saturate((w.x-.5)*1.6+.5),w.y);
}
float skyDetailWeight(float footprint){return 1-smoothstep(317.0/16,317.0/4,footprint);}
float skyHeightFraction(float3 p){return saturate((p.z-skyCloud.x)/(skyCloud.y-skyCloud.x));}
// Storm front (sky_step_front): the deck amount at a world position, from
// its distance upwind of the eye between the leading and trailing edges.
float skyDeckAt(float2 xy){
 float along=dot(xy-skyEye.xy,skyDeckAxis.xy),w=skyDeckAxis.z;
 return smoothstep(skyDeckCloud.z,skyDeckCloud.z+w,along)*(1-smoothstep(skyDeckCloud.w-w,skyDeckCloud.w,along));
}
// Local cloud weather (types and coverage/density) for the position being
// sampled: the fair sky (skyTypes, skyCloud.zw, plus the pre-storm small
// clouds forming over the eye) blended into the deck (skyDeckTypes,
// skyDeckCloud.xy). Set before each density evaluation; skyT.w is the local
// closure.
static float4 skyT=0;static float2 skyCW=0;
void skyLocalWeather(float2 xy){
 float c=skyDeckAt(xy);
 float pre=skyDeckAxis.w*(1-smoothstep(3000,8000,length(xy-skyEye.xy)));
 skyT=lerp(skyTypes,skyDeckTypes,c);
 skyCW=lerp(float2(saturate(skyCloud.z+.35*pre),skyCloud.w),skyDeckCloud.xy,c);
}
// ---- cloud types (Nubis-style scalar type: 0 stratus, .5 cumulus, 1 cumulonimbus)
// Large-scale type noise (features of about 15 km) drifting with the wind.
float skyCloudTypeNoise(float2 xy){
 float n=skyWeatherMap.SampleLevel(skyWrap,skyNoiseCoord(float3(xy,0),0).xy+float2(.53,.29),0).y;
 return saturate((n-.5)*2.5+.5);
}
float skyCloudType(float n){return saturate(skyT.x+skyT.y*(n*2-1));}
// Weights of stratus, cumulus and nimbus for a type value.
// A closing deck takes the scattered cumulus share into its own body (stratus,
// nimbus as it storms): every type path from stratus to nimbus crosses .5,
// and a closed deck must never break into fair cumulus there.
float3 skyTypeWeights(float t){
 float s=saturate(1-2*t),n=saturate(2*t-1),c=1-s-n,k=c*skyT.w;
 return float3(s+k*(1-skyT.z),c-k,n+k*skyT.z);
}
// Stratus fills a sheet, nimbus a broad mass, cumulus stays scattered.
float skyTypeCoverage(float3 tw){return dot(tw,float3(1.3,1,1.2));}
// Nimbus is heavier, darkest while it precipitates.
float skyTypeExtinction(float3 tw){return dot(tw,float3(.85,1,1.15+.55*skyT.z));}
// Stratus evens out the coverage noise into a sheet with few holes.
float skyTypeWeather(float w,float3 tw){return lerp(w,.6+.4*w,tw.x);}
float skyLocalCoverage(float w,float3 tw){
 float open=saturate((w-(1-saturate(skyCW.x*skyTypeCoverage(tw))))*2.5);
 // A closed storm deck is separate heavy masses over the kilometre-scale
 // coverage cores with gaps between them; the thin upper sheet
 // (skyCloudDensityLocal) closes the sky above, so the gaps show lighter
 // grey, never clear sky under rain. Runtime (b1c5ef9, real rain): a deck
 // that filled every column read at the game camera's 1-8 degrees as one
 // flat band; dark scalloped mass bases against lighter gaps and far deck
 // are what reads at grazing angles.
 return lerp(open,saturate((w-.35)*3),skyT.w);
}
float skyBand(float h,float b0,float b1,float t0,float t1){return saturate((h-b0)/(b1-b0))*saturate((t1-h)/max(t1-t0,1e-4));}
// Vertical density profile over the layer fraction h (800 m .. 4800 m) for
// local coverage wc, variation v and storm mass m; local is the height inside
// the blended cloud (0 base, 1 top) for erosion and ambient light.
float skyCloudProfile(float h,float wc,float v,float m,float3 tw,out float local,out float base){
 // A closed deck hangs lower over the kilometre-scale coverage cores (m 1)
 // than between them (m 0). Kept shallow: a 560 m seam lift raised the base
 // so far that the eye-anchored deck edge cut a hard wavy line across the
 // view (runtime, db97b88).
 float seam=skyT.w*(1-m);
 // Stratus: a low sheet with a gently varying underside, 800-1300 m.
 float sT=.08+.05*v;
 float sB=.012*v+.02*seam;
 // Cumulus/altocumulus: broad, rounded lobes. The existing weather
 // variation moves the base; thin column edges lift it into the body.
 float core=sqrt(wc);
 float cB=.075+.05*v+.05*(1-core),cT=cB+.03+.47*core*lerp(.5,1,v);
 // nimbus: low ragged base, towers up to the layer top
 // A closed storm base undulates over the kilometre-scale variation field:
 // low-hanging masses and high recesses, large enough to read at the low
 // angles the game camera sees the sky (per-lobe detail averages out there).
 float nB=.02+lerp(.05,.17,skyT.w)*(1-v)+.04*(1-wc)+.05*seam,nT=.5+.5*core-.1*seam;
 // Keep the existing inexpensive nimbus envelope; its varying base and
 // body noise already round it. Cubic ramps here repeated in every shadow
 // column added cost without helping the fair-weather cumulus underside.
 // A closed storm base ramps in over twice the height, so its lobes hang
 // deeper (vertical relief of several hundred metres).
 // Issue #115: one body per column. Summing the three weighted bands put a
 // stratus sheet (800-1300 m) under a cumulus body starting 1100-1500 m in
 // every mixed-type column (fair type .22-.88 spans stratus/cumulus), both
 // cut by the same coverage and shape noise: each cloud showed a flat copy
 // of its own outline a few hundred metres below it. The type weights now
 // blend the band edges instead, so a mixed column is one cloud between the
 // types' bases and tops; a pure type keeps its own band exactly (cumulus
 // smooth edges, stratus and nimbus linear).
 float b0=dot(tw,float3(sB,cB,nB));base=b0;
 float b1=dot(tw,float3(sB+.025,cB+.07,nB+lerp(.08,.17,skyT.w)));
 float t0=dot(tw,float3(sT*.55,lerp(cB,cT,.55),lerp(nB,nT,.7))),t1=dot(tw,float3(sT,cT,nT));
 float bandL=skyBand(h,b0,b1,t0,t1);
 float bandS=smoothstep(b0,b1,h)*(1-smoothstep(t0,t1,h));
 float lb=dot(tw,float3(0,cB,nB));  // stratus height inside its sheet starts at the layer base
 local=saturate((h-lb)/max(t1-lb,1e-4));
 return lerp(bandL,bandS,tw.y);
}
// One density model for sky clouds, the probe and the cloud shadow map.
// shade: the closed deck's underside tone (1 neutral), constant within a
// billow, for the march's ambient (skyMarchClouds).
float skyCloudDensityLow(float3 p,bool detail,float footprint,out float local,out float shade){
 local=0;shade=1;
 skyLocalWeather(p.xy);
 float h=skyHeightFraction(p);
 float2 w=skyWeatherAt(p.xy);
 float3 tw=skyTypeWeights(skyCloudType(skyCloudTypeNoise(p.xy)));
 float wc=skyLocalCoverage(skyTypeWeather(w.x,tw),tw);
 // A storm deck ends some kilometres out (eye-anchored): low rays see its
 // edge and the bright sky beyond, as under a real storm cell.
 float edge=skyRain.w>0?1-skyRain.w*smoothstep(skyStormLight.w*.65,skyStormLight.w,length(p.xy-skyEye.xy)):1;
 wc*=edge;
 // The closed deck's thin upper sheet (altostratus, 2.5-3.2 km): lighter
 // than the masses below it, gently varying, no 3D taps.
 float upper=skyT.w*edge*skyBand(h,.42,.47,.52,.6)*.22*(.6+.4*w.y)*skyCW.y;
 [branch]if(upper>0&&wc<=.001){local=.8;return upper;}
 if(wc<=.001)return 0;
 float m=smoothstep(.2,.8,w.x);
 float base;
 float profile=skyCloudProfile(h,wc,w.y,m,tw,local,base);
 if(profile<=0){[branch]if(upper>0)local=.8;return upper;}  // above this column's cloud: no 3D taps
 // Capped below 1 so a closed deck keeps crevices between its billows; a
 // full threshold filled the whole layer and showed one flat grey base.
 float coverage=min(wc*lerp(.75,1,max(tw.x,tw.z)),.93);
 float s0=skyShapeNoise.SampleLevel(skyWrap,skyShapeCoord(p,3)+float3(.13,.71,.29),0);
 float s1=skyShapeNoise.SampleLevel(skyWrap,skyShapeCoord(p,4)+float3(.67,.19,.83),0);
 float s=s0*.65+s1*.35;
 // Closed deck underside (seen from below it is opaque within one step and
 // takes no direct light, so only this tone shapes it): the strong billows
 // that hang lowest (130-500 m, a few degrees of view at the game camera's
 // distances) catch the light as rounded grey highlights, the pockets between
 // them stay dark, and the thick cores read a little darker than the seams.
 // It follows the shape noise, not the sample height: a height-based relief
 // depended on where a march step landed (dark near slab, bright far band).
 // Wide range: the march averages several samples, the reconstruction
 // blends frames, and the display curve compresses dark tones (runtime: the
 // .42-1.6 range showed only when contrast-stretched).
 shade=lerp(.25,2.1,smoothstep(.4,.74,s))*lerp(1.2,.7,m);
 s=lerp(s,.7+.3*s,tw.x*.7);
 // Shape threshold selects the cloud body; the vertical envelope rounds
 // that body. Thresholding their product shaved sparse desert banks into
 // disconnected slivers with unnaturally level lower edges.
 // Rounded bases (cloud-shape, user: "fluffy clouds splattered with a
 // plane from the bottom"): a height-independent threshold started every
 // cloud body at one base height out to its thin edges, so from below and at
 // mid angles the base read as a flat cut with a flat skirt. Over the lowest
 // ~440 m the threshold now rises (Nubis-style envelope in the threshold):
 // the strong cores still sit on the common condensation level, weaker shape
 // noise toward the edges only starts higher, so the underside curves up into
 // the sides and its density ramps in softly. The closed deck keeps its own
 // tuned underside.
 // Storm nimbus has its own envelope threshold below (dn).
 // Runtime (2bf28538, Gipat pitch 5): large clouds still showed wide flat
 // bottoms, the rounding only reached their edges. The broad shape layer
 // (s1, ~3.5 km) now also lifts the rounding start by up to ~240 m where it
 // is weak, so a large cloud's underside is scalloped into lobes at slightly
 // different heights while its strong cores keep the common base; the
 // rounding is deeper (520 m) and starts from a lower floor.
 // Runtime (559a91a2, looking up): many tiny detached puffs ("confetti")
 // around the large clouds, where a weak coverage edge or the low rounding
 // floor let only isolated noise maxima through. Fair cells below ~.2
 // coverage fade out, and the floor is .42.
 float storm=saturate(skyT.z+skyT.w);
 coverage*=lerp(smoothstep(.1,.28,coverage),1,storm);
 float lift=.06*saturate((.62-s1)*1.6);
 float br=saturate((h-base-lift)/.13);
 coverage*=lerp(lerp(.42,1,br),1,storm);
 float d=saturate(skyRemap(s,1-coverage,1,0,1))*profile;
 // Nimbus: the envelope enters the threshold (Nubis), so only the strongest
 // billows reach down to its base: a lumpy, hanging underside with dark
 // pockets instead of a level cut. The dense body above stays connected.
 float dn=saturate(skyRemap(s,1-coverage*saturate(profile*1.4),1,0,1))*sqrt(profile);
 d=lerp(d,dn,tw.z*saturate(skyT.z+skyT.w));  // fair cumulus unchanged
 float dw=detail?skyDetailWeight(footprint):0;
 [branch]if(dw>0&&d>0){
  float dn=skyDetailNoise.SampleLevel(skyWrap,skyNoiseCoord(p,5)+float3(.41,.23,.59),0);
  // Height-dependent erosion: wispy, torn bases and billowy, rounded tops;
  // nimbus bases tear harder.
  float e=lerp(dn,1-dn,saturate(local*4));
  d=saturate(skyRemap(d,e*(.16+.16*tw.z*(1-local))*(1-local*.3)*dw,1,0,1));
 }
 return d*skyCW.y*skyTypeExtinction(tw)+upper;
}
// Mid-level altocumulus sheet (issue #115 follow-up): a thin layer of small
// cells over km-scale patches, drifting faster and veered against the low
// volume (sky_mid_phases), so the sky and the ground dapple move in layers.
// Two taps, only inside its 3.4-3.9 km band.
float skyMidDensity(float3 p){
 float hb=skyBand(skyHeightFraction(p),skyMidBand.x,skyMidBand.y,skyMidBand.z,skyMidBand.w);
 [branch]if(hb<=0||skyMid.z<=0)return 0;
 float2 q=float2(.573576*p.x-.819152*p.y,.819152*p.x+.573576*p.y)/skyMidWavelength.x+skyMid.xy;
 float c=skyWeatherMap.SampleLevel(skyWrap,q,0).x;
 float cover=saturate((c-(1-skyMid.z))*3);
 [branch]if(cover<=0)return 0;
 float3 r=float3(.939693*p.x-.342020*p.y,.342020*p.x+.939693*p.y,p.z/SKY_SHAPE_DEPTH)/skyMidWavelength.y+skyMidShape.xyz;
 float s=skyShapeNoise.SampleLevel(skyWrap,r,0);
 return saturate(skyRemap(s,1-cover*.8,1,0,1))*hb*skyMid.w;
}
float skyCloudDensityLocal(float3 p,bool detail,float footprint,out float local,out float shade){
 float d=skyCloudDensityLow(p,detail,footprint,local,shade);
 float m=skyMidDensity(p);
 [branch]if(m>0)local=d>0?lerp(local,.6,m/(m+d)):.6;
 return d+m;
}
float skyCloudDensity(float3 p,bool detail,float footprint){float local,shade;return skyCloudDensityLocal(p,detail,footprint,local,shade);}
)"
R"(
float3 skyCloudAmbient(float h){
 float3 zenith=skyBaseRadiance(float3(0,0,1)),side=skyBaseRadiance(normalize(float3(-skyCloudLight.xy,.15)));
 float3 sky=(zenith+side)*.5;
 float3 bottom=sky*.55*float3(.92,.9,1.05)+skyCloudBottom.rgb*saturate(skyCloudLight.z)*.04;
 // A precipitating base sees little sky: blue-black with a cool teal cast.
 bottom*=lerp(1,float3(.3,.37,.42),skyT.z);
 // A closed deck takes no twilight tint from the dome: a cool slate tone,
 // dark blue-black at its base. Warm light reaches it only from below.
 float3 top=sky*1.2;
 bottom=lerp(bottom,dot(bottom,float3(.3,.5,.2))*float3(.78,.9,1.15),skyT.w*.9);
 top=lerp(top,dot(top,float3(.3,.5,.2))*float3(.86,.93,1.07),skyT.w*.75);
 return lerp(bottom,top,h);
}
float skyHash2(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float skyValueNoise(float2 p){
 float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(skyHash2(i),skyHash2(i+float2(1,0)),f.x),lerp(skyHash2(i+float2(0,1)),skyHash2(i+1),f.x),f.y);
}
// The same noise repeating every `period` cells (seamless across a phase wrap).
float skyValueNoisePeriodic(float2 p,float period){
 float2 i=floor(p),f=frac(p),j=i+1;f=f*f*(3-2*f);
 i-=floor(i/period)*period;j-=floor(j/period)*period;
 return lerp(lerp(skyHash2(i),skyHash2(float2(j.x,i.y)),f.x),lerp(skyHash2(float2(i.x,j.y)),skyHash2(j),f.x),f.y);
}
// Low sun under a storm deck: its light enters through the deck's far edge
// (radius skyStormLight.w around the eye) and lights the base, fading with
// the distance it travels beneath the deck. Linear radiance factor.
float skyUnderReach(float2 xy,float2 sunXY){
 float2 q=xy-skyEye.xy;float b=dot(q,sunXY);
 float x=max(0,-b+sqrt(max(0,b*b-dot(q,q)+skyStormLight.w*skyStormLight.w)));  // 0 beyond the edge
 return exp(-x/5000);
}
// Lightning lights the deck around its flash point (multiple scattering
// spreads it over a few kilometres of cloud).
float3 skyFlashLight(float3 p){return float3(.8,.86,1)*skyFlash.w*7*exp(-length(p-skyFlash.xyz)/1800);}
// Rain shafts under the storm deck: grey-blue curtains hanging from the base
// where the coverage field is dense, slanted downwind, broken into streaks
// across the wind. Six taps between 1.5 km and the base (at most 24 km).
float4 skyRainShafts(float3 ro,float3 rd,float tBase){
 float tEnd=min(tBase,24000);
 [branch]if(skyRain.x<=0||tEnd<=1500)return float4(0,0,0,1);
 float2 dir=skyRain.yz;
 skyLocalWeather(ro.xy+rd.xy*tEnd);
 float3 col=skyCloudAmbient(0);  // dark curtains against the bright gap
 float ratio=pow(tEnd/1500,1.0/6),t=1500;float3 S=0;float T=1;
 [loop]for(int i=0;i<6;++i,t*=ratio){
  float dt=t*(ratio-1),tm=t*sqrt(ratio);
  float3 p=ro+rd*tm;
  float2 src=p.xy-dir*max(0,skyCloud.x-p.z)*.35;
  float2 nc=skyNoiseCoord(float3(src,0),1).xy;  // moves with the deck
  // The masses' own coverage field, so the curtains hang from the dark
  // masses and the gaps between them stay clear.
  float w=skyWeatherAt(src).x;
  float streak=.5+.5*skyValueNoisePeriodic(frac(nc)*80,80);  // ~240 m curtains
  float sigma=skyRain.x*smoothstep(.5,.75,w)*streak*skyDeckAt(src)/5000;  // only under the deck
  float tr=exp(-sigma*dt);
  float3 c=col;[branch]if(skyFlash.w>0)c+=skyFlashLight(float3(p.xy,skyCloud.x))*.15;
  S+=T*c*(1-tr);T*=tr;
 }
 // Curtains veil at most 55% of the deck behind them. Runtime (real rain,
 // bbd810d): 1/3000 m curtains over the 5-20 km to a low ray's base covered
 // the whole visible low sky in one uniform tone, an ultra flat rain sky.
 float k=min(1,.55/max(1-T,1e-4));
 return float4(S*k,1-(1-T)*k);
}
// Detail footprint of a march sample: the pixel cone width at that distance,
// but never finer than a quarter step (jittered steps average the rest).
float skyMarchFootprint(float t,float dt,float pixelAngle){return max(t*pixelAngle,dt*.25);}
// The march spans the layer up to 20 km along the ray, so low rays reach the
// tops of distant towers instead of a squeezed slice of the layer's base.
// Samples are spaced geometrically (each covers the same share of distance),
// which follows the growing pixel footprint; the count follows that log span.
static const float SKY_MARCH_SPAN=20000;
int skyMarchSteps(float t0,float t1,int steps){
 return clamp((int)ceil(steps*log(t1/t0)/log(3.0)),max(6,steps/3),steps*4/3);
}
float4 skyMarchClouds(float3 ro,float3 rd,int steps,int lightSteps,float jitter,float maxDistance,float pixelAngle){
 if(rd.z<=0)return float4(0,0,0,1);
 float3 o=float3(0,0,SKY_RG+ro.z);
 float t0=max(10,skyRaySphere(o,rd,SKY_RG+skyCloud.x).y),t1=skyRaySphere(o,rd,SKY_RG+skyCloud.y).y;
 float4 rain=skyRainShafts(ro,rd,t0);
 skyLocalWeather(ro.xy+rd.xy*t0);float rayDeck=skyT.w;  // ray-level tone: the deck where the ray enters the layer
 t1=min(t1,t0+SKY_MARCH_SPAN);if(t0>=maxDistance||t1<=t0)return rain;
 int n=skyMarchSteps(t0,t1,steps);
 float ratio=pow(t1/t0,1.0/n),cosT=dot(rd,skyCloudLight.xyz),sigma=.035;
 float3 S=0;float T=1;
 float3 ambient0=skyCloudAmbient(0),ambient1=skyCloudAmbient(1);
 bool under=skyStormLight.w>0&&any(skyStormLight.rgb>0);
 float2 sunXY=normalize(skySun.xy+float2(1e-5,0));
 float underPhase=lerp(.5,1.6,saturate(dot(rd,skySun.xyz)*.5+.5));
 float t=t0*pow(ratio,jitter);
 [loop]for(int i=0;i<n&&T>.01;++i,t*=ratio){
  float dt=t*(ratio-1);
  float3 pc=o+rd*t;float alt=length(pc)-SKY_RG;
  float3 p=float3(ro.xy+rd.xy*t,alt);
  float local;
  float shade;
  float d=skyCloudDensityLocal(p,true,skyMarchFootprint(t,dt,pixelAngle),local,shade);
  if(d<=0)continue;
  float od=0,ls=80;float3 lp=p;
  [loop]for(int j=0;j<lightSteps;++j){lp+=skyCloudLight.xyz*ls;od+=skyCloudDensity(lp,j<2,ls)*ls;ls*=1.9;}
  // three-octave multiple-scattering approximation (soft, painterly light);
  // a narrow forward lobe on the first octave gives sunlit silver linings
  float3 light=0;float a=1,b=1,g=1;
  [unroll]for(int k=0;k<3;++k){
   float phase=lerp(skyPhaseHG(cosT,.6*g),skyPhaseHG(cosT,-.2*g),.3)+(k==0?.2*skyPhaseHG(cosT,.85):0);
   light+=a*exp(-sigma*od*b)*phase;a*=.55;b*=.4;g*=.5;
  }
  // Beer-powder: thin sunward edges have little in-scattered light behind
  // them; strongest when the sun is behind the viewer (dark crevices).
  float powder=1-.7*exp(-2*sigma*(od+d*60));
  powder=lerp(powder,1,saturate(cosT*.5+.5)*.6);
  // Sky ambient from above, occluded by the cloud still overhead; the height
  // is the sample's height inside its own cloud type.
  float3 ambient=lerp(ambient0,ambient1,local)*lerp(1,exp(-sigma*d*(1-local)*60),.6);
  // A closed deck seen from below is opaque within one march step, so its
  // billows read only through shading: the lowest points of hanging lobes
  // catch the horizon light, the crevices between them stay dark, thin
  // fringes glow. (Runtime: a uniform ambient made the deck one flat tone.)
  // Absolute height: the low-hanging masses are lit, the recesses dark.
  float hb=saturate((skyHeightFraction(p)-.02)/.14);
  ambient*=lerp(1,shade,skyT.w);
  float3 irradiance=lerp(skyCloudBottom.rgb,skyCloudTop.rgb,skyHeightFraction(p));
  float3 scatter=irradiance*light*4*SKY_PI*powder+ambient;
  [branch]if(under){float b=1-hb;b*=b;scatter+=skyStormLight.rgb*skyUnderReach(p.xy,sunXY)*b*b*underPhase;}
  [branch]if(skyFlash.w>0)scatter+=skyFlashLight(p);
  float transmitted=exp(-sigma*d*dt);
  S+=T*scatter*(1-transmitted);T*=transmitted;
 }
 // aerial perspective: distant clouds fade toward the horizon sky
 // A closed storm deck keeps its dark, structured base far out instead of
 // dissolving into the bright overcast dome (runtime: it washed out flat).
 float fade=exp(-max(0,t0-4000)/lerp(30000,9000,saturate(skyWeather.x)*(1-.75*rayDeck)));
 // Dissolve at the existing finite sky/probe reach instead of dropping a
 // whole bank when the first layer intersection crosses that boundary.
 fade*=1-smoothstep(maxDistance-SKY_MARCH_SPAN,maxDistance,t0);
 // Rain hangs in front of the deck.
 return float4(rain.rgb+rain.a*S*fade,rain.a*lerp(1,T,fade));
}
)"
R"(
// ------------------------------------------------------------ composition
float skyIgn(float2 p){return frac(52.9829189*frac(dot(p,float2(.06711056,.00583715))));}
// Linear sky radiance behind the clouds: atmosphere, sun or moon disc, stars.
float3 skyBackground(float3 rd,bool discs){
 float3 sky=skyBaseRadiance(rd);
 float night=skyMoon.w,overcast=skyWeather.y;
 // a faint teal astral glow low on the night horizon (allods float in the aether)
 sky+=float3(.05,.35,.32)*skyLutMoon.w*night*exp(-abs(rd.z)*18);
 // Lightning glows through the deck around its direction.
 [branch]if(skyFlash.w>0){float3 f=normalize(skyFlash.xyz-skyEye.xyz);sky+=float3(.8,.86,1)*skyFlash.w*(.05+.25*pow(saturate(dot(rd,f)),6));}
 [branch]if(discs&&rd.z>0){
  // The low sky in a storm deck's edge gap stays clear (skyBaseRadiance).
  float clear=1-overcast*.9*(1-skyRain.w*(1-smoothstep(0,.3,rd.z)));
  float cs=dot(rd,skySun.xyz);
  float3 sunTint=skyLightRadiance.rgb*.5+.5*dot(skyLightRadiance.rgb,.33);
  [branch]if(skySun.z>-.02){
   float disc=smoothstep(skySunDisc.x,skySunDisc.y,cs);
   float halo=pow(saturate(cs),900)*.6+pow(saturate(cs),24)*.08*(1-overcast);
   sky+=sunTint*(disc*skySunDisc.z+halo)*clear*skySun.w*smoothstep(-.02,.09,skySun.z);   // rises in over ~6 degrees
  }
  [branch]if(skyMoonDisc.w>0&&skyMoon.z>-.02){
   // moon: small soft disc with faint maria and a faint halo, dimmed in twilight
   float cm=dot(rd,skyMoon.xyz);
   float disc=smoothstep(skyMoonDisc.x,skyMoonDisc.y,cm);
   float3 side=normalize(cross(skyMoon.xyz,float3(0,0,1))+float3(1e-4,0,0)),up=cross(side,skyMoon.xyz);
   float maria=.8+.2*skyValueNoise(float2(dot(rd,side),dot(rd,up))*180+7);
   float halo=pow(saturate(cm),2000)*.25+pow(saturate(cm),60)*.02;
   sky+=float3(.82,.9,1.15)*(disc*maria+halo)*skyMoonDisc.z*clear*saturate(skyMoon.z*30+.6);
  }
  [branch]if(night>0){
   // stars: brighter when clear, dimmed by haze and overcast (clouds cover them below)
   float3 sp=rd*420,cell=floor(sp);
   float hs=skyHash2(cell.xy+cell.z*17.13),hb=skyHash2(cell.yz+3.1);
   float3 c=cell+.5+(float3(hs,hb,skyHash2(cell.zx))-.5)*.6;
   float star=(hs>.992?1:0)*smoothstep(.35,.05,length(sp-c))*(.3+hb);
   sky+=star*.02*skyLutSun.w*skyStars.rgb*night*saturate(rd.z*6)*(1-saturate(skyWeather.x*2))*clear;
  }
 }
 return sky;
}
// The approved prototype grade: exposure, cool night shift, painterly warm
// highlights / cool shadows, soft exponential curve, display gamma.
float3 skyDisplay(float3 c){
 c*=skyLightRadiance.w;
 float ln=dot(c,float3(.2126,.7152,.0722));
 c=lerp(c,ln*float3(.6,.78,1.15),.6*skyMoon.w);
 float l=dot(c,float3(.2126,.7152,.0722));
 c=lerp(c,c*lerp(float3(.92,.96,1.08),float3(1.06,1,.92),saturate(l*1.5)),1);
 c=max(0,lerp(l.xxx,c,.92));
 c=1-exp(-c*1.25);
 return pow(c,1/2.2);
}
// Final display colour; the horizon band and everything below it meet the
// fog's haze colour, so distant terrain and the sky agree.
// High cirrus: a thin 2D ice layer far above the volume, streaked along the
// upper wind (texture taps only, no march). rgb radiance, a opacity.
// pixelAngle (radians) selects the weather-map mip so the fine streaks do not
// shimmer near the horizon.
float skyCirrusLod(float footprint,float scale){return max(0,log2(footprint*512/scale));}
float4 skyCirrusLayer(float3 rd,float pixelAngle){
 if(skyCirrus.z<=0||rd.z<=.01)return 0;
 float3 o=float3(0,0,SKY_RG+skyEye.z);
 float t=skyRaySphere(o,rd,SKY_RG+skyCirrus.w).y;
 float2 xy=skyEye.xy+rd.xy*t,dir=skyCirrus.xy;
 float2 q=float2(dot(xy,dir),dot(xy,float2(-dir.y,dir.x)));
 // footprint on the layer: across the view, and stretched along it at low elevation
 float footprint=t*pixelAngle*rsqrt(max(rd.z,.01));
 float a=skyWeatherMap.SampleLevel(skyWrap,q/skyCirrusScale.xy+skyCirrusPhase.xy,skyCirrusLod(footprint,skyCirrusScale.y)).x;
 float b=skyWeatherMap.SampleLevel(skyWrap,q/skyCirrusScale.zw+skyCirrusPhase.zw,skyCirrusLod(footprint,skyCirrusScale.w)).y;
 // Upper layer readable from the game camera (cloud-shape, user: "natural
 // thin clouds ... the missing upper layer"): the camera sees 3-10 degrees,
 // where this layer lies 50-160 km out, and the old fades (exp(-t/90 km),
 // full only above 7 degrees) left at most ~.03-.15 opacity there. Softer
 // fades, a slightly higher floor on thin amounts, and a faint milky
 // altostratus veil from the same tap; still thin wisps, not a deck.
 float amount=lerp(skyCirrus.z,sqrt(skyCirrus.z),.5);
 float wisp=saturate((a*.6+b*.4-(1-amount))*2.2);
 float veil=saturate((b-.4)*1.6)*amount*.1;
 float alpha=(wisp*wisp*.45+veil*(1-wisp*wisp*.45))*saturate((rd.z-.01)*25)*exp(-t/250000);
 // forward-scattering ice lit by the sun or moon, plus the zenith sky
 float cosT=dot(rd,skyCirrusLight.w>0?skySun.xyz:skyMoon.xyz);
 float3 c=skyCirrusLight.rgb*(skyPhaseHG(cosT,.5)*SKY_PI+.35)+skyBaseRadiance(float3(0,0,1))*.9;
 return float4(c,alpha);
}
float3 skyFinal(float3 rd,float4 clouds,bool discs,float pixelAngle){
 // Sun, moon and stars sit behind every cloud: the bright discs take a much
 // stronger cloud attenuation than the sky, or a thin edge lets them shine
 // through as if drawn over the clouds.
 float3 back=skyBackground(rd,false);
 [branch]if(discs){float3 d=skyBackground(rd,true)-back;back+=d*clouds.a*clouds.a*clouds.a*clouds.a;}
 // Cirrus lies behind the whole volume: the cloud transmittance covers it.
 [branch]if(clouds.a>0){float4 ci=skyCirrusLayer(rd,pixelAngle);back=lerp(back,ci.rgb,ci.a);}
 float3 c=skyDisplay(back*clouds.a+clouds.rgb);
 // A thin band only, and only where the sky shows through: clouds in the
 // horizon band (what the game camera sees) keep their shape and light.
 // Below the horizon only the haze shows: blending the below-horizon sky
 // radiance (dark) into it drew a dark band under the horizon line, and the
 // border fog, which takes its colour from this probe, laid the same dark
 // gradient over distant terrain. Above, the haze eases into the sky.
 float k=lerp(40,18,skyStars.w);
 float rise=exp(-max(rd.z,0)*k);
 float w=rd.z<0?1:lerp(skyHorizon.w*rise*clouds.a,1,rise*rise);
 return lerp(c,skyHorizon.rgb,w);
}
float3 skyRay(float2 uv){
 float4 p=mul(skyClipToWorld,float4(uv.x*2-1,1-uv.y*2,1,1));
 return normalize(p.xyz/p.w-skyEye.xyz);
}

// ------------------------------------------------------------ passes
// Temporal cloud reconstruction. The march runs at half the reconstruction
// resolution and each frame traces one sub-pixel of every 2x2 block, so four
// frames trace every reconstruction pixel. Clouds are effectively at infinity:
// the previous reconstruction is reprojected by direction and clamped to the
// current march neighbourhood (bounds ghosting); fast turns fall back to the
// upsampled current march instead of smearing.
[numthreads(8,8,1)]void skyCloudsCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=skySize.xy))return;
 float2 uv=(id.xy*2+skyReconstruct.zw+.5)/skyReconstruct.xy;
 float3 rd=skyRay(uv);
 float4 current=float4(0,0,0,1);
 [branch]if(rd.z>0){
  float pixelAngle=length(skyRay(uv+float2(1.0/skyReconstruct.x,0))-rd);
  current=skyMarchClouds(skyEye.xyz,rd,(int)skyControl.y,(int)skyControl.z,frac(skyIgn(id.xy)+skyControl.x*.618034),60000,pixelAngle);
 }
 skyColorOut[id.xy]=current;
}
// The vertical component's sign of skyRay(uv), without the normalisation.
float skyRayHeight(float2 uv){
 float4 p=mul(skyClipToWorld,float4(uv.x*2-1,1-uv.y*2,1,1));
 return p.z/p.w-skyEye.z;
}
[numthreads(8,8,1)]void skyResolveCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=skyReconstruct.xy))return;
 int2 last=(int2)skySize.xy-1,m=min((int2)(id.xy/2),last);
 // Well below the horizon: every march texel this pixel reads (the 3x3
 // neighbourhood and the bilinear footprint, within one texel of m) holds
 // the march's below-horizon value (0,0,0,1), so the result is exactly that.
 // The rays below the horizon form a half-plane on screen: the four corners
 // of a box two march texels wider than those texels (either jitter) decide.
 {
  float2 lo=(float2(max(m-3,0))*2+.5)/skyReconstruct.xy,hi=(float2(min(m+3,last))*2+1.5)/skyReconstruct.xy;
  [branch]if(skyRayHeight(lo)<0&&skyRayHeight(hi)<0&&skyRayHeight(float2(lo.x,hi.y))<0&&skyRayHeight(float2(hi.x,lo.y))<0){
   skyColorOut[id.xy]=float4(0,0,0,1);return;
  }
 }
 bool fresh=all((id.xy&1)==skyReconstruct.zw)&&all(id.xy/2<skySize.xy);
 // The exact sample traced this frame, or the march moved by this frame's jitter.
 float4 current=fresh?skyClouds.Load(int3(m,0))
  :skyClouds.SampleLevel(skyClamp,((float2(id.xy)-float2(skyReconstruct.zw))*.5+.5)/skySize.xy,0);
 float4 result=current;
 [branch]if(skyWeather.w>0){
  float2 uv=(id.xy+.5)/skyReconstruct.xy;
  float4 previous=mul(skyPreviousWorldToClip,float4(skyRay(uv),0));
  if(previous.w>1e-4){
   float2 historyUv=float2(previous.x/previous.w*.5+.5,.5-previous.y/previous.w*.5);
   float moved=length((historyUv-uv)*skyReconstruct.xy);
   float keep=saturate(1.5-moved/16)*(fresh?skyWeather.w:.95);
   [branch]if(all(historyUv>0)&&all(historyUv<1)&&keep>0){
    float4 lo=current,hi=current;
    [unroll]for(int k=0;k<9;++k){float4 v=skyClouds.Load(int3(clamp(m+int2(k%3-1,k/3-1),0,last),0));lo=min(lo,v);hi=max(hi,v);}
    result=lerp(current,clamp(skyHistory.SampleLevel(skyClamp,historyUv,0),lo,hi),keep);
   }
  }
 }
 skyColorOut[id.xy]=result;
}
// Equirectangular reflection probe (sky and clouds, display colour) for water.
[numthreads(8,8,1)]void skyProbeCS(uint3 id:SV_DispatchThreadID){
 uint width=skyControl.w,height=width/2;
 if(any(id.xy>=uint2(width,height)))return;
 float2 uv=(id.xy+.5)/float2(width,height);
 float az=uv.x*2*SKY_PI,el=(.5-uv.y)*SKY_PI;
 float3 d=float3(cos(el)*cos(az),cos(el)*sin(az),sin(el));
 float4 clouds=skyMarchClouds(skyEye.xyz,d,max(8,(int)skyControl.y/3),2,frac(skyIgn(id.xy)+skyControl.x*.618034),40000,2*SKY_PI/width);
 float3 c=skyFinal(d,clouds,false,2*SKY_PI/width);
 if(skyPrevious.w>0)c=lerp(c,skyHistory.Load(int3(id.xy,0)).rgb,.85);
 skyColorOut[id.xy]=float4(c,1);
}
// Ground transmittance along the game's light, for the world and the fog.
float skyCloudOpticalDepth(float2 ground,int samples){
 // The footprint offset (height x slope) is capped at 0.6 (about 59 degrees
 // of light elevation): with the full slope a low or turning light swept the
 // footprint of a 2-4 km cloud several kilometres within a game hour, which
 // read as shadows racing (play-2: "sometimes super fast") or standing still
 // where that sweep cancelled the drift. The game camera never sees the
 // cloud and its shadow together, so a steeper projection costs nothing.
 float3 s=skyLight.xyz;float2 slope=s.xy/max(s.z,.15);float sl=length(slope);slope*=sl>.6?.6/sl:1;
 float invz=sqrt(1+dot(slope,slope));
 float range=skyCloud.y-skyCloud.x,od=0;
 // Quadratic spacing: fine samples through the low stratus sheet and the
 // cumulus bases, coarse ones through the tall nimbus tops.
 [loop]for(int i=0;i<samples;++i){
  float u0=(float)i/samples,u1=(i+1.0)/samples;
  float dz=range*(u1*u1-u0*u0),z=skyCloud.x+range*(u0*u0+u1*u1)*.5;float3 p=float3(ground+slope*z,z);
  // Integrate the visible density; an unrelated erosion lattice used to
  // put shadow gaps where the sky had solid cloud.
  od+=skyCloudDensity(p,true,dz*invz)*dz*invz;
 }
 return od;
}
float skyCloudShadowResponse(float od){
 // Density columns are measured in metres. The old .5..8 threshold made
 // almost any cloud edge immediately opaque. A 200 m optical-depth scale
 // spreads the arrival, and cores remove at most 35% of direct light.
 // A gentle drifting dimming of the direct sun only (sky and ambient light
 // stay): thick cores take at most 40%, thin edges little (play-2 user
 // verdict: cloud shadows must never read as deep, crisp object shadows; the
 // 60% cap of dcf39a8c is undone).
 return 1-.4*(1-exp(-max(0,od)/250));
}
float skyCloudTransmittance(float2 ground,int samples){
 // Broad spatial penumbra, anchored to the same advected density as the
 // visible cloud. Average columns before the soft optical response; no
 // temporal lag that would leave shadows behind their clouds.
 // Very soft, wide edges (play-2 user verdict): a 150 m penumbra, so a
 // passing cloud dims the view gradually instead of a crisp edge sweeping.
 const float radius=150;
 float od=skyCloudOpticalDepth(ground,samples)*.4;
 od+=skyCloudOpticalDepth(ground+float2(radius,0),samples)*.15;
 od+=skyCloudOpticalDepth(ground-float2(radius,0),samples)*.15;
 od+=skyCloudOpticalDepth(ground+float2(0,radius),samples)*.15;
 od+=skyCloudOpticalDepth(ground-float2(0,radius),samples)*.15;
 return skyCloudShadowResponse(od);
}
// The cloud-shadow receivers' blur, once per texel of the field: the five
// taps skyCloudVisibility took per pixel (6 m apart, the centre twice, the
// same clamp bilinear reads). The receiver then samples this field once;
// the blur and the bilinear read swap order (equal for a field linear
// across the 6 m taps, otherwise within its curvature over one texel).
[numthreads(8,8,1)]void skyShadowBlurCS(uint3 id:SV_DispatchThreadID){
 uint width,height;skyShadowOut.GetDimensions(width,height);
 if(any(id.xy>=uint2(width,height)))return;
 float2 uv=(id.xy+.5)/float2(width,height);
 float o=6/skyShadow.z;
 skyShadowOut[id.xy]=(2*skyShadowRaw.SampleLevel(skyClamp,uv,0)+
  skyShadowRaw.SampleLevel(skyClamp,uv+float2(o,o*.3),0)+skyShadowRaw.SampleLevel(skyClamp,uv-float2(o,o*.3),0)+
  skyShadowRaw.SampleLevel(skyClamp,uv+float2(-o*.3,o),0)+skyShadowRaw.SampleLevel(skyClamp,uv-float2(-o*.3,o),0))/6;
}
[numthreads(8,8,1)]void skyShadowCS(uint3 id:SV_DispatchThreadID){
 uint width,height;skyShadowOut.GetDimensions(width,height);
 if(any(id.xy>=uint2(width,height)))return;
 // Cloud shadows off (strength 0): t=lerp(1,T,0)=1 and 1+(1-1)*w=1 below.
 [branch]if(skyLight.w==0){skyShadowOut[id.xy]=1;return;}
 float2 ground=skyShadow.xy+(id.xy+.5)/float2(width,height)*skyShadow.z;
 float t=lerp(1,skyCloudTransmittance(ground,12),skyLight.w);
 skyShadowOut[id.xy]=t+(1-t)*skyShadow.w;
}
float4 skyVS(uint id:SV_VertexID):SV_Position{
 float2 p=float2((id<<1)&2,id&2);
 return float4(p.x*2-1,1-p.y*2,1,1);
}
float4 skyPS(float4 position:SV_Position):SV_Target{
 float2 uv=position.xy/float2(skySize.zw);
 float3 rd=skyRay(uv);
 float4 clouds=rd.z>0?skyClouds.SampleLevel(skyClamp,uv,0):float4(0,0,0,1);
 float pixelAngle=length(skyRay(uv+float2(1.0/skySize.z,0))-rd);
 return float4(skyFinal(rd,clouds,true,pixelAngle),1);
}
)";
#undef SKY_HLSL_LAYER
#undef SKY_STRING
#undef SKY_STRING_LIST
#undef SKY_STRING_LIST_INNER
#undef SKY_STRING_INNER
#undef SKY_NOISE_LAYERS
#undef SKY_CIRRUS_SCALE
#undef SKY_MID_BAND
#undef SKY_MID_COVERAGE_WAVELENGTH
#undef SKY_MID_SHAPE_WAVELENGTH
#undef SKY_SHAPE_DEPTH_TILES

// Native world pixel hooks (compiled into the native shader before the liquid
// surface shader; needs the fog frame constants). The probe replaces the flat
// fog-colour sky that liquids reflected where screen-space reflection misses.
constexpr char kSkyNativeShader[] = R"(
#define LIQUID_OWNED_SKY 1
float3 liquidOwnedSky(float3 r,float3 skyColor,float zenith){
 [branch]if(fogTemporal.z<.5)return skyColor*lerp(1,zenith,saturate(r.z));
 float az=atan2(r.y,r.x)*(.5/3.14159265);az+=az<0?1:0;
 return skyProbe.SampleLevel(fogLinearClamp,float2(az,.5-asin(clamp(r.z,-1,1))/3.14159265),0).rgb;
}
)";

}  // namespace owned
