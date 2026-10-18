#pragma once
// Exact inline replacements for std::isfinite and std::nextafter(value,
// +/-infinity). With /fp:precise on x86, MSVC lowers both to CRT calls
// (_fdtest/_dtest, _nextafterf); in the per-frame bounds tests they were
// ~5% of the main thread in a pan sample. Same results for every input.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace owned {

inline bool finite_value(float value) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline bool finite_value(double value) noexcept {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

// std::nextafter(value, toward) for a finite value and an infinite target;
// anything else goes to the library.
inline float next_float(float value, float toward) noexcept {
    if (!finite_value(value) || finite_value(toward) || toward != toward)
        return std::nextafter(value, toward);
    const bool up = toward > 0;
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if (!(bits & 0x7fffffffu)) bits = up ? 1u : 0x80000001u;   // either zero
    else if (((bits >> 31) == 0) == up) ++bits;                 // away from zero
    else --bits;                                                // toward zero
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

} // namespace owned
