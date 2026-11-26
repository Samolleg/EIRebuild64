#pragma once
// rt_emissive: the game's additive particles (the particle systems with the
// game's add effect: sparks, healing, protection spheres, portals, lightning
// blasts) and lightning ribbons as point lights. Pure CPU code: no D3D, no
// engine state.
//
// The game keeps the particles (game_particles.h); an additive particle call
// or a bolt is light the frame shows but no CLight stands for unless the
// effect's author added one. The renderer gathers every additive world
// particle and every bolt segment of a frame as a sample (world position,
// colour x area), groups the samples into
// clusters that persist across frames, and publishes one lamp a cluster after
// the game's and the lava lights (set_native_point_lights), so the light
// grid, the lamp loop, the ray traced lamp light and the lamp at a bounce
// ray's hit all handle it.
//
// Units: the lighting factor (what a lamp adds before the texture). A
// particle of colour c over a quad of area A sends c * A * cos / (pi * d^2)
// to a surface at distance d; a cluster's power P is the sum of c * A. The
// published lamp has the game's falloff, colour * (1 - d^2 / R^2)^2: R is
// where the inverse-square value falls to kEdgeFactor, but never beyond the
// distance where a uniformly bright disc of the cluster's own size falls to
// kEdgeFactor of its surface brightness (kReachPerSize disc radii): a small
// effect around a unit (a haste or regeneration cloud of many overlapping
// sparkles) lights close to the unit, however many particles it stacks. The
// colour makes the two falloffs agree at R / 3. The peak is never above the cluster's own
// brightness, its power over the disc it fills (a surface beside a flame is
// not brighter than the flame).
// Buffs and spell sparkles use the same size/power estimate as a local accent,
// capped at 1.5 m and 0.5 per channel at strength 1, preserving the effect's hue.
// Their clusters never mix with flames or bolts. Interface particle types
// never enter the samples.
// Steady CampFire/Fire sources have their own policy: 4-8 m reach from the
// same size/power estimate, without diluting their peak by the larger radius.
// They never join spell clusters; fireball/firewall keep the disc model above.
//
// Stability: a cluster's power, place and spread follow their targets
// exponentially in game seconds (frozen on pause, scaled with game speed); a
// cluster no sample reached fades out. Steady flames bridge short sampling
// gaps before fading, with no radius threshold that switches a dim lamp off.
// Game lights do not own these lamps (user decision 2026-11-16).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "texture_mips.h"

namespace owned::particle_lights {

// The supported game's particle type values, carried by GameParticleView.
// Path/selection indicators are interface even though their draw is in the
// world pass. A missing producer identity must never turn into a guessed lamp.
inline bool casts_no_light(std::uint32_t type) noexcept {
    return type == 0 || (type >= 0x2039u && type <= 0x203bu); // PATH, DESTINATION, FAILED
}

inline bool local_glow(std::uint32_t type) noexcept {
    return type == 0x2006u || type == 0x200bu || // healing, casting
        (type >= 0x2014u && type <= 0x202fu) || // link, protection, modifiers, casting sparkles
        (type >= 0x2041u && type <= 0x204du) || // stars, sparks, status effects (FEETCLOUD2 = 0x204b)
        type == 0x204fu || type == 0x2050u;    // curse stars/holder
}

inline bool steady_flame(std::uint32_t type) noexcept {
    return type == 0x2001u || type == 0x2003u; // CampFire, Fire; not FireWall
}

struct Sample {
    std::array<float, 3> position{};
    std::array<float, 3> power{};   // colour x area, m2
    float area = 0;                 // m2
    bool local_glow = false;
    bool steady_flame = false;
};

struct Cluster {
    std::uint32_t id = 0;
    std::array<float, 3> centre{};
    std::array<float, 3> power{};
    float area = 0, spread = 0;     // area: the disc the cluster fills (its spread plus a mean particle), m2
    // One update's sums.
    std::array<double, 3> sum_position{}, sum_power{};
    double sum_weight = 0, sum_area = 0, sum_spread = 0;
    std::uint32_t sum_count = 0;
    bool fresh = true;
    bool local_glow = false;        // never joins/merges with an unrestricted flame or bolt
    bool steady_flame = false;      // separate from spell fire, even at the same place
    float missing_seconds = 0;      // game time since the last accepted flame sample
};

struct Light {
    std::uint32_t id = 0;
    std::array<float, 3> centre{};
    float radius = 0;
    std::array<float, 3> color{};
    float spread = 0;               // the cluster's size: the sphere its shadow rays aim at
    bool steady_flame = false;
    bool local_glow = false;        // a buff or sparkle cloud: lit as a sphere of its spread (#263)
};

constexpr float kJoin = 1.5f;           // a sample joins the nearest cluster within this, metres
constexpr float kOwner = 2.0f;          // a game light this near owns the cluster
constexpr float kPowerSeconds = .25f;   // game seconds
constexpr float kPlaceSeconds = .08f;
constexpr float kEdgeFactor = .03f;     // the lamp's radius: where the inverse-square factor falls to this
constexpr float kMatch = 3.f;           // the falloffs agree at radius / kMatch
constexpr float kSmallestRadius = 1.f;  // spell/bolt cutoff; local accents and steady flames fade below it
// Buff/sparkle lighting is an accent at the effect, not a room light. These
// limits apply after accumulation, so overlapping particles cannot raise them.
constexpr float kLocalRadius = 1.5f;
constexpr float kLocalStrength = .5f;   // at most this per channel at strength 1
constexpr float kFlameReachScale = 2.5f; // the observed 2.2 m torch becomes about 5.5 m
constexpr float kFlameMinRadius = 4.f, kFlameMaxRadius = 8.f;
constexpr float kFlameHoldSeconds = .25f, kFlameFadeSeconds = .5f;
constexpr std::size_t kMaxSteadyLights = 64; // oldest cluster IDs first: flicker cannot reorder the budget
// A disc of radius a lit uniformly sends a^2 / (a^2 + d^2) of its surface value
// along its axis: that falls to kEdgeFactor at d = a * sqrt(1 / kEdgeFactor - 1).
constexpr float kReachPerSize = 5.6862407f;
constexpr float kPi = 3.14159265f;

inline float luminance(const std::array<float, 3>& c) noexcept { return .2126f * c[0] + .7152f * c[1] + .0722f * c[2]; }

inline std::uint64_t cell_key(const float* p) noexcept {
    const auto axis = [](float v) { return std::uint64_t(std::uint32_t(std::int32_t(std::floor(v / kJoin))) & 0x1FFFFFu); };
    return axis(p[0]) | (axis(p[1]) << 21) | (axis(p[2]) << 42);
}

// One world frame's samples into the clusters. dt: game seconds since the
// last update (0 while paused: existing clusters keep their state, new ones
// start at their target). owners: the centres of the game's lights. Returns
// how many clusters with samples this update a game light owns.
inline std::size_t update(std::vector<Cluster>& clusters, std::uint32_t& next_id, const std::vector<Sample>& samples,
                   float dt, const std::vector<std::array<float, 3>>& owners) {
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells;
    cells.reserve(clusters.size() * 2 + 16);
    for (std::uint32_t n = 0; n < clusters.size(); ++n) {
        auto& c = clusters[n];
        c.sum_position = {}; c.sum_power = {}; c.sum_weight = c.sum_area = c.sum_spread = 0; c.sum_count = 0;
        cells[cell_key(c.centre.data())].push_back(n);
    }
    const auto distance_squared = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
        return x * x + y * y + z * z;
    };
    std::uint32_t last = UINT32_MAX;
    for (const Sample& s : samples) {
        const float weight = luminance(s.power);
        if (!(weight > 0) || !(s.area > 0)) continue;
        std::uint32_t found = UINT32_MAX;
        // An emitter's particles follow each other: the last cluster first.
        if (last != UINT32_MAX && clusters[last].local_glow == s.local_glow && clusters[last].steady_flame == s.steady_flame &&
            distance_squared(clusters[last].centre, s.position) <= kJoin * kJoin * .25f) found = last;
        if (found == UINT32_MAX) {
            float best = kJoin * kJoin;
            for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const float at[3] = {s.position[0] + dx * kJoin, s.position[1] + dy * kJoin, s.position[2] + dz * kJoin};
                const auto cell = cells.find(cell_key(at));
                if (cell == cells.end()) continue;
                for (const std::uint32_t n : cell->second) {
                    if (clusters[n].local_glow != s.local_glow || clusters[n].steady_flame != s.steady_flame) continue;
                    const float d = distance_squared(clusters[n].centre, s.position);
                    if (d < best || (d == best && found != UINT32_MAX && clusters[n].id < clusters[found].id)) { best = d; found = n; }
                }
            }
        }
        if (found == UINT32_MAX) {
            Cluster c; c.id = next_id++; c.centre = s.position; c.local_glow = s.local_glow; c.steady_flame = s.steady_flame;
            found = std::uint32_t(clusters.size());
            clusters.push_back(c);
            cells[cell_key(s.position.data())].push_back(found);
        }
        Cluster& c = clusters[found];
        for (int a = 0; a < 3; ++a) { c.sum_position[a] += double(s.position[a]) * weight; c.sum_power[a] += s.power[a]; }
        c.sum_weight += weight; c.sum_area += s.area; ++c.sum_count;
        c.sum_spread += double(distance_squared(c.centre, s.position)) * weight;
        last = found;
    }
    std::size_t owned_count = 0;
    const float follow_power = dt > 0 ? 1.f - std::exp(-dt / kPowerSeconds) : 0.f;
    const float follow_place = dt > 0 ? 1.f - std::exp(-dt / kPlaceSeconds) : 0.f;
    for (Cluster& c : clusters) {
        std::array<float, 3> target_power{}, target_centre = c.centre;
        float target_area = 0, target_spread = c.spread;
        if (c.sum_weight > 0) {
            for (int a = 0; a < 3; ++a) {
                target_centre[a] = float(c.sum_position[a] / c.sum_weight);
                target_power[a] = float(c.sum_power[a]);
            }
            target_spread = float(std::sqrt(c.sum_spread / c.sum_weight));
            target_area = kPi * target_spread * target_spread + float(c.sum_area / c.sum_count);
        }
        bool owned = false;
        for (const auto& owner : owners)
            if (distance_squared(owner, target_centre) <= kOwner * kOwner) { owned = true; break; }
        if (owned) { target_power = {}; if (c.sum_weight > 0) ++owned_count; }
        float power_follow = follow_power;
        if (c.steady_flame) {
            if (c.sum_weight > 0) c.missing_seconds = 0;
            else {
                // Hold only a previously observed flame, never invent an emitter.
                // Integrate just the part of this step beyond the grace period.
                const float before = (std::max)(c.missing_seconds - kFlameHoldSeconds, 0.f);
                c.missing_seconds += (std::max)(dt, 0.f);
                const float after = (std::max)(c.missing_seconds - kFlameHoldSeconds, 0.f);
                power_follow = 1.f - std::exp(-(after - before) / kFlameFadeSeconds);
            }
        }
        if (c.fresh) {
            c.centre = target_centre; c.power = target_power; c.area = target_area; c.spread = target_spread; c.fresh = false;
            continue;
        }
        for (int a = 0; a < 3; ++a) {
            c.power[a] += (target_power[a] - c.power[a]) * power_follow;
            if (c.sum_weight > 0) c.centre[a] += (target_centre[a] - c.centre[a]) * follow_place;
        }
        if (c.sum_weight > 0) {
            c.area += (target_area - c.area) * follow_power;
            c.spread += (target_spread - c.spread) * follow_power;
        }
    }
    if (dt > 0) {
        // Clusters that drifted together become one (the older keeps its identity); the faded ones go.
        for (std::size_t a = 0; a < clusters.size(); ++a) for (std::size_t b = a + 1; b < clusters.size(); ++b) {
            Cluster& first = clusters[a]; Cluster& second = clusters[b];
            if (first.local_glow != second.local_glow || first.steady_flame != second.steady_flame) continue;
            if (second.area < 0 || first.area < 0) continue;
            if (distance_squared(first.centre, second.centre) > kJoin * kJoin * .25f) continue;
            Cluster& keep = first.id < second.id ? first : second; Cluster& gone = first.id < second.id ? second : first;
            for (int axis = 0; axis < 3; ++axis) keep.power[axis] += gone.power[axis];
            keep.missing_seconds = (std::min)(keep.missing_seconds, gone.missing_seconds);
            if (keep.steady_flame) keep.sum_weight += gone.sum_weight; // a sampled merge must not retire as missing
            keep.area += gone.area; gone.area = -1;
        }
        clusters.erase(std::remove_if(clusters.begin(), clusters.end(), [](const Cluster& c) {
            return c.area < 0 || (c.sum_weight <= 0 && luminance(c.power) < 1e-3f);
        }), clusters.end());
    }
    return owned_count;
}

// The clusters' lamps, in ascending id order. strength scales the colour.
inline void lights(const std::vector<Cluster>& clusters, float strength, std::vector<Light>& out) {
    out.clear();
    for (const Cluster& c : clusters) {
        const float power = luminance(c.power);
        if (!(power > 0) || !(c.area > 0) || !(strength > 0)) continue;
        const float disc = std::sqrt(c.area / kPi);
        float radius = (std::min)(std::sqrt(power / (kPi * kEdgeFactor)), disc * kReachPerSize);
        if (c.local_glow) radius = (std::min)(radius, kLocalRadius);
        if (!(radius > 0) || !std::isfinite(radius)) continue;
        if (!c.steady_flame && !c.local_glow && radius < kSmallestRadius) continue;
        // Keep the original flame peak when extending its reach. Recomputing it
        // at the enlarged radius would divide its light by reachScale squared.
        // For flames and local accents below 1 m, a fixed match distance makes
        // the peak tend smoothly to zero with power, without a radius cutoff.
        const float match_radius = c.steady_flame || c.local_glow ? (std::max)(radius, kSmallestRadius) : radius;
        if (c.steady_flame) radius = std::clamp(radius * kFlameReachScale, kFlameMinRadius, kFlameMaxRadius);
        const float at = match_radius / kMatch, fall = 1.f - 1.f / (kMatch * kMatch);
        // The cluster's hue at the reference match distance, never above its own brightness.
        float peak = (std::min)(power / (kPi * at * at) / (fall * fall), power / c.area) * strength;
        if (c.local_glow) {
            // Bound the brightest channel with one scale for the whole colour:
            // stacked sparkles cannot inflate the accent or bleach its hue.
            const float brightest = (std::max)(c.power[0], (std::max)(c.power[1], c.power[2]));
            peak = (std::min)(peak, kLocalStrength * strength * power / brightest);
        }
        Light light; light.id = c.id; light.centre = c.centre; light.radius = radius;
        light.steady_flame = c.steady_flame; light.local_glow = c.local_glow;
        light.spread = c.local_glow ? (std::min)(c.spread, radius) : c.spread;
        for (int a = 0; a < 3; ++a) light.color[a] = (std::max)(c.power[a], 0.f) / power * peak;
        if (!std::isfinite(light.color[0] + light.color[1] + light.color[2] + light.centre[0] + light.centre[1] + light.centre[2])) continue;
        out.push_back(light);
    }
    std::sort(out.begin(), out.end(), [](const Light& a, const Light& b) { return a.id < b.id; });
    std::size_t steady_count = 0;
    out.erase(std::remove_if(out.begin(), out.end(), [&](const Light& light) {
        return light.steady_flame && ++steady_count > kMaxSteadyLights;
    }), out.end());
}

// The mean colour, colour x alpha and coverage of the submitted UV rectangle.
// RGB masks or DXT1/3/5 (also the stored DXT2/4 values, as on the GPU). Read
// compressed blocks through the existing texture decoder, without expanding
// or retaining a second atlas. A flame frame must not average in smoke frames.
// False: not readable.
inline bool texture_mean(const std::uint8_t* pixels, std::size_t bytes, std::uint32_t width, std::uint32_t height, long pitch,
                         std::uint32_t bits, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha,
                         float rgb[3], float rgb_alpha[3], float v0 = 0.f, float v1 = 1.f, float* mean_alpha = nullptr,
                         float u0 = 0.f, float u1 = 1.f, unsigned dxt = 0) noexcept {
    const std::uint32_t size = bits / 8u;
    if (!pixels || !width || !height || pitch <= 0) return false;
    if (dxt ? (dxt != 1 && dxt != 3 && dxt != 5) :
        ((bits != 16 && bits != 24 && bits != 32) || !red || !green || !blue)) return false;
    const std::uint32_t block_bytes = dxt == 1 ? 8u : 16u;
    const std::uint64_t row_bytes = dxt ? ((std::uint64_t(width) + 3u) / 4u) * block_bytes : std::uint64_t(width) * size;
    const std::uint64_t rows = dxt ? (std::uint64_t(height) + 3u) / 4u : height;
    if (std::uint64_t(pitch) < row_bytes || std::uint64_t(pitch) * (rows - 1u) + row_bytes > bytes) return false;
    if (!(std::isfinite(v0) && std::isfinite(v1) && std::isfinite(u0) && std::isfinite(u1))) return false;
    const float low = std::clamp((std::min)(v0, v1), 0.f, 1.f), high = std::clamp((std::max)(v0, v1), 0.f, 1.f);
    const float left = std::clamp((std::min)(u0, u1), 0.f, 1.f), right = std::clamp((std::max)(u0, u1), 0.f, 1.f);
    if (!(high > low) || !(right > left)) return false;
    const std::uint32_t first = (std::min)(height - 1u, std::uint32_t(low * float(height)));
    const std::uint32_t end = (std::max)(first + 1u, (std::min)(height, std::uint32_t(std::ceil(high * float(height)))));
    const std::uint32_t first_x = (std::min)(width - 1u, std::uint32_t(left * float(width)));
    const std::uint32_t end_x = (std::max)(first_x + 1u, (std::min)(width, std::uint32_t(std::ceil(right * float(width)))));
    const auto channel = [](std::uint32_t value, std::uint32_t mask) {
        if (!mask) return 1.f;
        unsigned shift = 0; while (!((mask >> shift) & 1u)) ++shift;
        return float((value & mask) >> shift) / float(mask >> shift);
    };
    const std::uint32_t step_x = (std::max)(1u, (end_x - first_x) / 256u), step_y = (std::max)(1u, (end - first) / 256u);
    double sum[3] = {0, 0, 0}, sum_alpha[3] = {0, 0, 0}, sum_a = 0; std::uint64_t count = 0;
    std::uint32_t decoded[16]{};
    const std::uint8_t* last_block = nullptr;
    for (std::uint32_t y = first; y < end; y += step_y) {
        const std::uint8_t* row = pixels + std::size_t(pitch) * (dxt ? y / 4u : y);
        for (std::uint32_t x = first_x; x < end_x; x += step_x) {
            std::uint32_t value = 0;
            if (dxt) {
                const auto* block = row + std::size_t(x / 4u) * block_bytes;
                if (block != last_block) { decode_dxt_level(block, 4, 4, dxt, decoded); last_block = block; }
                value = decoded[(y % 4u) * 4u + x % 4u];
            } else for (std::uint32_t b = 0; b < size; ++b) value |= std::uint32_t(row[std::size_t(x) * size + b]) << (8u * b);
            const float c[3] = {channel(value, dxt ? 0xff0000u : red), channel(value, dxt ? 0xff00u : green),
                channel(value, dxt ? 0xffu : blue)}, a = channel(value, dxt ? 0xff000000u : alpha);
            for (int k = 0; k < 3; ++k) { sum[k] += c[k]; sum_alpha[k] += double(c[k]) * a; }
            sum_a += a;
            ++count;
        }
    }
    for (int k = 0; k < 3; ++k) { rgb[k] = float(sum[k] / double(count)); rgb_alpha[k] = float(sum_alpha[k] / double(count)); }
    if (mean_alpha) *mean_alpha = float(sum_a / double(count));
    return true;
}

// Glowing, not smoke: how much an alpha-blended particle's visible colour c
// (0..1) is flame or magic light. The same key the bloom glow target takes for
// alpha-blended particles (game_particles.h gpFlameKey): bright (max channel
// 0.5 to 0.9) and saturated (0.35 to 0.65); grey or dark smoke, dust and
// blood give 0.
inline float flame_key(const float c[3]) noexcept {
    const auto smooth = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); };
    const float hi = (std::max)(c[0], (std::max)(c[1], c[2])), lo = (std::min)(c[0], (std::min)(c[1], c[2]));
    return smooth(.5f, .9f, hi) * smooth(.35f, .65f, (hi - lo) / (std::max)(hi, 1e-4f));
}

} // namespace owned::particle_lights
