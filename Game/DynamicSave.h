#pragma once
#include "SaveSchema.h"
#include <array>

namespace SaveSchema {
// Capture/apply only on the simulation thread at a segment boundary. Loading
// this value stages bytes; only P17's successful world commit calls Apply().
struct RandomState
{
    bool ready = false;
    std::uint32_t count = 0;
    std::array<std::uint32_t, 256> results{}, memory{};
    std::uint32_t a = 0, b = 0, c = 0, nativeCrt = 1;
    static RandomState Capture();
    void Apply() const;
    void Store(Writer &) const;
    void Load(Reader &);
};
} // namespace SaveSchema
