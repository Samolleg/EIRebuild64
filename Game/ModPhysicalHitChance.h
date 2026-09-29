#pragma once

#include <stdint.h>

// Probability of DetermineHit's ordinary physical attack at the current
// positions: no called shot, backstab, magic or active-skill accuracy bonus.
// This is a pure preview: it must never advance either game's RNG.
namespace PhysicalHitChance {

class PreviewLimiter {
    uint32_t last[2];
    bool emitted[2];
public:
    PreviewLimiter() : last(), emitted() {}
    bool Allow(unsigned direction, uint32_t now) {
        if (direction >= 2) return false;
        if (emitted[direction] && uint32_t(now - last[direction]) < 1000u)
            return false;
        emitted[direction] = true;
        last[direction] = now;
        return true;
    }
};

inline float Score(int attack, float spread, float distancePenalty, uint32_t roll)
{
    float random = 0.0f;
    if (spread > 0.0f)
        random = 2.0f * roll - (spread + 1.0f);
    const float score = static_cast<float>(attack) + random - distancePenalty;
    return score > 0.0f ? score : 0.0f;
}

inline double DefensePass(int attack, int defense, float spread,
    uint32_t roundedSpread, float distancePenalty)
{
    if (spread <= 0.0f)
        return Score(attack, spread, distancePenalty, 0) >= defense ? 1.0 : 0.0;

    // Common saturated matchups need neither binary search nor 64-bit division.
    if (Score(attack, spread, distancePenalty, 0) >= defense) return 1.0;
    if (Score(attack, spread, distancePenalty, roundedSpread) < defense) return 0.0;

    // RandomGenerator::Get(n) is Get() % (n + 1). Find the first successful
    // discrete roll, preserving float evaluation and the inclusive >= test.
    const uint64_t count = static_cast<uint64_t>(roundedSpread) + 1;
    uint64_t low = 0, high = count;
    while (low < high) {
        const uint64_t mid = low + (high - low) / 2;
        if (Score(attack, spread, distancePenalty, static_cast<uint32_t>(mid)) >= defense)
            high = mid;
        else
            low = mid + 1;
    }
    // Include the small modulo bias of the 32-bit generator.
    const uint64_t domain = uint64_t(1) << 32;
    const uint64_t remainder = domain % count;
    const uint64_t successes = (count - low) * (domain / count) +
        (low < remainder ? remainder - low : 0);
    return static_cast<double>(successes) / 4294967296.0;
}

inline double AvoidProbability(float power)
{
    // Exact count for (rand() * 100 / 32767) % 100 < power.
    // The endpoint rand()==32767 wraps to zero and contributes one.
    // No distribution table, initialization loop or per-preview scan.
    if (!(power > 0.0f)) return 0.0; // Includes NaN, as in the comparison loop.
    if (power >= 100.0f) return 1.0;
    const unsigned whole = static_cast<unsigned>(power);
    const unsigned buckets = whole + (power > whole ? 1 : 0);
    const unsigned avoided = (buckets * 32767u + 99u) / 100u + 1u;
    return static_cast<double>(avoided) / 32768.0;
}

inline double Percent(int attack, int defense, float spread,
    uint32_t roundedSpread, float distancePenalty, float avoidPower, bool stunned)
{
    return 100.0 * DefensePass(attack, defense, spread, roundedSpread, distancePenalty) *
        (stunned ? 1.0 : 1.0 - AvoidProbability(avoidPower));
}
}
