#pragma once
#include <cstdint>

namespace Navigation {
// Shared work allowance per host iteration, plus one bounded batch per unit.
// Neither clock nor allowance depends on the optional performance recorder.
struct PathWorkBudget {
    double remaining = 2.;
    std::uint64_t frame = ~std::uint64_t(0);
    double* ForFrame(std::uint64_t current) noexcept {
        if (frame != current) { frame = current; remaining = 2.; }
        return &remaining;
    }
};
struct PathProgress {
    std::uint64_t frame = ~std::uint64_t(0);
    bool FirstBatch(std::uint64_t current) noexcept {
        if (frame == current) return false;
        frame = current;
        return true;
    }
};
}
