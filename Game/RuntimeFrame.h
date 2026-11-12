#pragma once
#include <cstdint>

// Required runtime progress, not profiling state. The host owns this on the
// game thread even when all optional timing/journal counters are disabled.
namespace RuntimeFrame {
class Sequence {
    std::uint64_t serial = 0;
public:
    void Advance() noexcept { ++serial; }
    std::uint64_t Current() const noexcept { return serial; }
};
inline Sequence host;
inline void Begin() noexcept { host.Advance(); }
inline std::uint64_t Current() noexcept { return host.Current(); }
}
