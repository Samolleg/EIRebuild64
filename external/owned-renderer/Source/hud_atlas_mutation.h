#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <vector>

namespace owned {

struct SurfaceImage;

struct HudAtlasMutationWindow {
    LONG atlas_width = 0;
    RECT rect{};
    DWORD expected_lock_flags = 0;
};

struct HudAtlasMutationSnapshot {
    std::uint64_t surface_id = 0;
    std::uint64_t generation = 0;
    UINT width = 0;
    UINT height = 0;
    LONG pitch = 0;
    DDPIXELFORMAT format{};
    RECT rect{};
    std::size_t row_bytes = 0;
    std::vector<std::uint8_t> before;
    bool active = false;
};

enum class HudAtlasMutationComparison : std::uint8_t {
    unavailable,
    unchanged,
    changed,
};

// This is intentionally checked before the normal Lock path can allocate or
// read back storage. A comparison is eligible only when the complete current
// CPU image already exists.
bool hud_atlas_cpu_pixels_authoritative(const SurfaceImage& image) noexcept;

// Called only after the normal DirectDraw Lock has succeeded. The two legacy
// producers write A1R5G5B5 bytes using atlas_width as their row stride. A
// snapshot is eligible only when those exact bytes were already authoritative
// on the CPU before Lock; this helper never requests a readback or GPU resource.
bool capture_hud_atlas_mutation(
    const SurfaceImage& image, const HudAtlasMutationWindow& window,
    const RECT& locked_rect, DWORD lock_flags,
    bool cpu_authoritative_before_lock, bool dc_active,
    HudAtlasMutationSnapshot& snapshot) noexcept;

HudAtlasMutationComparison compare_hud_atlas_mutation(
    const SurfaceImage& image,
    const HudAtlasMutationSnapshot& snapshot) noexcept;

// Applies the normal writable-Unlock mutation accounting. On an exact no-op it
// preserves all pre-existing dirty/generation state. On a changed producer
// rectangle it dirties only that rectangle; unavailable comparisons retain the
// original locked-rectangle behavior. Returns true only for a suppressed no-op.
bool finish_hud_atlas_surface_write(
    SurfaceImage& image, DWORD lock_flags, const RECT& locked_rect,
    HudAtlasMutationSnapshot& snapshot,
    bool& increment_uniqueness) noexcept;

} // namespace owned
