#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The renderer owns all shadows, so the game's software shadows never run,
// whatever "Shadow Settings" holds in the registry. Supported image only:
// every consumer of ShadowQuality (0x79bd0c) gates on it being > 0, and the
// work it gates is reached through two branches. Each branch is rewritten from
// "je target" to "jmp target", the path stock already takes when shadows are
// off. The settings themselves are never written, so the options screen and
// the saved registry values stay the player's.
//
//  - Draw3DScene 0x56b983: test of the world's render-shadows flag (0x200,
//    set from ShadowQuality at world creation). Skipping it removes the
//    per-object DrawRealisticShadow2 calls: CPU projection, 64x64 software
//    raster/blur, shadow-texture blit, terrain casting and the legacy draw.
//  - Visibility pass 0x56ae9f: an object outside the view whose stock shadow
//    might be visible gets its skeleton recalculated and joins a shadow list.
//    Forcing "not visible" removes that work; the lists have no other reader.
struct StockShadowBranch {
    std::uint32_t context_rva;
    std::array<std::uint8_t, 13> stock;  // verified bytes ending in "0f 84 rel32"
    std::size_t size;
};

constexpr std::array<StockShadowBranch, 2> kStockShadowBranches{{
    {0x0016b983, {0x8b,0x46,0x08,0xf6,0xc4,0x02,0x0f,0x84,0xf5,0x02,0x00,0x00}, 12},
    {0x0016ae9f, {0xe8,0x5c,0x34,0xfb,0xff,0x84,0xc0,0x0f,0x84,0x9d,0x00,0x00,0x00}, 13},
}};

// Rewrites the trailing near je of a verified window into a jmp to the same
// target. A window that is not exactly stock (another owner, other build) is
// refused unchanged.
inline bool stock_shadow_branch_force(std::uint8_t* window,
                                      const StockShadowBranch& branch) noexcept {
    if (std::memcmp(window, branch.stock.data(), branch.size) != 0) return false;
    std::uint8_t* const jcc = window + branch.size - 6;
    std::int32_t relative = 0;
    std::memcpy(&relative, jcc + 2, sizeof(relative));
    relative += 1;  // jmp rel32 is one byte shorter than je rel32
    jcc[0] = 0xe9;
    std::memcpy(jcc + 1, &relative, sizeof(relative));
    jcc[5] = 0x90;
    return true;
}

inline bool stock_shadow_branch_forced(const std::uint8_t* window,
                                       const StockShadowBranch& branch) noexcept {
    std::array<std::uint8_t, 13> expected = branch.stock;
    return stock_shadow_branch_force(expected.data(), branch) &&
        std::memcmp(window, expected.data(), branch.size) == 0;
}
