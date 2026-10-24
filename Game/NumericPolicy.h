#pragma once
#include "Tools.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>

// Former x87 chains use double intermediates and only their original float stores.
// Keep operation order under /fp:precise; do not enable /fp:fast or /fp:contract.
#pragma float_control(precise, on, push)
namespace NumericPolicy {

// Match Tools.h conversions, but do not narrow the input through float.
// CVTSD2SI reads the current MXCSR RC (nearest/even in ordinary execution).
// With masked invalid, NaN/infinity/overflow produce signed32 indefinite.
inline int RoundToInt32(double value)
{
    return _mm_cvtsd_si32(_mm_set_sd(value));
}

inline int RoundToInt32(double value, FloatRounding mode)
{
    if (mode == FloatRounding::Chop)
        return _mm_cvttsd_si32(_mm_set_sd(value));
    const ScopedFloatRounding rounding(mode);
    return RoundToInt32(value);
}

inline float Experience(float experience, int count, uint32_t charge, float factor)
{
    double result;
    if (charge == 1)
        result = double(count) * experience;
    else if (charge == 2)
        result = double(count) + experience;
    else
        result = double(experience) / count;
    return float(result * factor);
}

inline float WeightBonus(float divisor, float attacker, float defender)
{
    const double denominator = double(divisor) * defender;
    return float(double(attacker) / denominator);
}

inline float DistanceModifier(float numerator, float divisor, float defenderRange)
{
    const double scaledRange = double(defenderRange) * divisor;
    const double denominator = scaledRange * 2.0;
    return float(double(numerator) / denominator);
}

// FILD treats the client's stored DWORD as signed32, without a float conversion.
inline int32_t SignedExperience(uint32_t bits)
{
    int32_t experience;
    std::memcpy(&experience, &bits, sizeof(experience));
    return experience;
}

inline int RebalancedHeroLevel(double experience, float divisor, float multiplier,
                               float addInside, float addOutside)
{
    const double quotient = experience / divisor;
    const double argument = quotient + addInside;
    const double logarithm = std::log2(argument);
    const double scaled = logarithm * multiplier;
    // The old FST to an unused float did not narrow ST(0) before FISTP.
    return RoundToInt32(scaled + addOutside);
}

// Caller retains the original float fPower store and unsigned ordinary client input.
inline int HeroLevelFromPower(float fPower, float base)
{
    return MINT(log(fPower) / log(base));
}

} // namespace NumericPolicy
#pragma float_control(pop)
