#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace MeleeCooldown {

// Historical conversion is intentional: 24.0 means 360 simulation segments.
const float LegacyTicksPerSecond = 15.0f;
const char MarkerPrefix[] = "__jmv_masc1_";

struct State
{
    uint32_t startTick;
    uint32_t totalTicks;
    uint32_t remainingAtStart;
};

inline uint32_t DurationTicks(float seconds)
{
    const float ticks = seconds * LegacyTicksPerSecond + 0.999f;
    if (!std::isfinite(seconds) || seconds <= 0.0f ||
        !std::isfinite(ticks) || static_cast<double>(ticks) > INT32_MAX)
        return 0;
    const uint32_t result = static_cast<uint32_t>(ticks);
    return result ? result : 1;
}

inline uint32_t Remaining(const State &state, uint32_t tick)
{
    const uint32_t elapsed = tick - state.startTick;
    return elapsed < state.remainingAtStart ? state.remainingAtStart - elapsed : 0;
}

// Store integers in the name, with a constant GS value. GS values are float;
// placing IDs or long tick counters in them would silently lose precision.
inline std::string Marker(uint32_t zone, uint64_t key, uint32_t total, uint32_t remaining)
{
    char name[80] = {};
    std::snprintf(name, sizeof(name), "%s%08X_%08X_%08X_%08X_%08X", MarkerPrefix,
                  zone, static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key),
                  total, remaining);
    return name;
}

inline bool ReadMarker(const char *name, uint32_t zone, uint64_t &key,
                       uint32_t &total, uint32_t &remaining)
{
    const size_t prefix = sizeof(MarkerPrefix) - 1;
    if (!name || std::strlen(name) != prefix + 44 ||
        std::strncmp(name, MarkerPrefix, prefix) != 0)
        return false;
    uint32_t words[5] = {};
    const char *p = name + prefix;
    for (unsigned word = 0; word < 5; ++word)
    {
        for (unsigned digit = 0; digit < 8; ++digit, ++p)
        {
            const int value = *p >= '0' && *p <= '9' ? *p - '0' :
                (*p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1);
            if (value < 0)
                return false;
            words[word] = (words[word] << 4) | static_cast<uint32_t>(value);
        }
        if (word < 4 && *p++ != '_')
            return false;
    }
    if (words[0] != zone || words[3] == 0 || words[3] > INT32_MAX ||
        words[4] == 0 || words[4] > words[3])
        return false;
    key = (static_cast<uint64_t>(words[1]) << 32) | words[2];
    total = words[3];
    remaining = words[4];
    return key != 0;
}

} // namespace MeleeCooldown
