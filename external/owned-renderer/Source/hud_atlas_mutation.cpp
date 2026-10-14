#include "hud_atlas_mutation.h"

#include "renderer.h"

#include <cstring>
#include <limits>
#include <new>

namespace owned {
namespace {

constexpr DWORD kA1R5G5B5RedMask = 0x7c00u;
constexpr DWORD kA1R5G5B5GreenMask = 0x03e0u;
constexpr DWORD kA1R5G5B5BlueMask = 0x001fu;
constexpr DWORD kA1R5G5B5AlphaMask = 0x8000u;
constexpr LONG kProducerPixelBytes = 2;

bool same_rect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

bool exact_producer_format(const DDPIXELFORMAT& format) noexcept {
    return (format.dwFlags & (DDPF_RGB | DDPF_ALPHAPIXELS)) ==
            (DDPF_RGB | DDPF_ALPHAPIXELS) &&
        (format.dwFlags & DDPF_FOURCC) == 0 &&
        format.dwRGBBitCount == 16 &&
        format.dwRBitMask == kA1R5G5B5RedMask &&
        format.dwGBitMask == kA1R5G5B5GreenMask &&
        format.dwBBitMask == kA1R5G5B5BlueMask &&
        format.dwRGBAlphaBitMask == kA1R5G5B5AlphaMask;
}

bool same_format(const DDPIXELFORMAT& left,
                 const DDPIXELFORMAT& right) noexcept {
    return left.dwFlags == right.dwFlags &&
        left.dwFourCC == right.dwFourCC &&
        left.dwRGBBitCount == right.dwRGBBitCount &&
        left.dwRBitMask == right.dwRBitMask &&
        left.dwGBitMask == right.dwGBitMask &&
        left.dwBBitMask == right.dwBBitMask &&
        left.dwRGBAlphaBitMask == right.dwRGBAlphaBitMask;
}

bool valid_image_bytes(const SurfaceImage& image) noexcept {
    if (image.pitch <= 0) return false;
    const auto pitch = static_cast<std::uint64_t>(image.pitch);
    const auto height = static_cast<std::uint64_t>(image.height);
    if (height && pitch > (std::numeric_limits<std::size_t>::max)() / height)
        return false;
    return image.pixels.size() >= static_cast<std::size_t>(pitch * height);
}

} // namespace

bool hud_atlas_cpu_pixels_authoritative(const SurfaceImage& image) noexcept {
    return !image.gpu_captured && valid_image_bytes(image);
}

bool capture_hud_atlas_mutation(
    const SurfaceImage& image, const HudAtlasMutationWindow& window,
    const RECT& locked_rect, DWORD lock_flags,
    bool cpu_authoritative_before_lock, bool dc_active,
    HudAtlasMutationSnapshot& snapshot) noexcept {
    snapshot = {};
    const RECT full{0, 0, static_cast<LONG>(image.width),
                    static_cast<LONG>(image.height)};
    if (!cpu_authoritative_before_lock || dc_active || image.gpu_captured ||
        lock_flags != window.expected_lock_flags ||
        !same_rect(locked_rect, full) || !exact_producer_format(image.format) ||
        !valid_image_bytes(image) || window.atlas_width <= 0 ||
        static_cast<std::int64_t>(image.pitch) !=
            static_cast<std::int64_t>(window.atlas_width) *
                kProducerPixelBytes)
        return false;

    const LONG copy_width = window.rect.right - window.rect.left;
    const LONG copy_height = window.rect.bottom - window.rect.top;
    if (window.rect.left < 0 || window.rect.top < 0 ||
        copy_width <= 0 || copy_height <= 0 ||
        window.rect.right > static_cast<LONG>(image.width) ||
        window.rect.bottom > static_cast<LONG>(image.height) ||
        window.rect.right > window.atlas_width)
        return false;

    const auto row_bytes = static_cast<std::size_t>(copy_width) *
        kProducerPixelBytes;
    const auto rows = static_cast<std::size_t>(copy_height);
    if (rows && row_bytes > (std::numeric_limits<std::size_t>::max)() / rows)
        return false;
    const auto snapshot_bytes = row_bytes * rows;
    if (snapshot_bytes > snapshot.before.max_size()) return false;
    try {
        snapshot.before.resize(snapshot_bytes);
    } catch (const std::bad_alloc&) {
        snapshot = {};
        return false;
    }
    for (LONG row = 0; row < copy_height; ++row) {
        const auto* source = image.pixels.data() +
            static_cast<std::size_t>(window.rect.top + row) * image.pitch +
            static_cast<std::size_t>(window.rect.left) * kProducerPixelBytes;
        std::memcpy(snapshot.before.data() +
                        static_cast<std::size_t>(row) * row_bytes,
                    source, row_bytes);
    }
    snapshot.surface_id = image.id;
    snapshot.generation = image.generation;
    snapshot.width = image.width;
    snapshot.height = image.height;
    snapshot.pitch = image.pitch;
    snapshot.format = image.format;
    snapshot.rect = window.rect;
    snapshot.row_bytes = row_bytes;
    snapshot.active = true;
    return true;
}

HudAtlasMutationComparison compare_hud_atlas_mutation(
    const SurfaceImage& image,
    const HudAtlasMutationSnapshot& snapshot) noexcept {
    if (!snapshot.active || image.gpu_captured ||
        image.id != snapshot.surface_id ||
        image.generation != snapshot.generation ||
        image.width != snapshot.width || image.height != snapshot.height ||
        image.pitch != snapshot.pitch ||
        !same_format(image.format, snapshot.format) ||
        !valid_image_bytes(image))
        return HudAtlasMutationComparison::unavailable;
    const LONG rows = snapshot.rect.bottom - snapshot.rect.top;
    if (rows <= 0 || snapshot.row_bytes == 0 ||
        snapshot.before.size() != snapshot.row_bytes *
            static_cast<std::size_t>(rows))
        return HudAtlasMutationComparison::unavailable;
    for (LONG row = 0; row < rows; ++row) {
        const auto* current = image.pixels.data() +
            static_cast<std::size_t>(snapshot.rect.top + row) * image.pitch +
            static_cast<std::size_t>(snapshot.rect.left) * kProducerPixelBytes;
        if (std::memcmp(current,
                        snapshot.before.data() +
                            static_cast<std::size_t>(row) * snapshot.row_bytes,
                        snapshot.row_bytes) != 0)
            return HudAtlasMutationComparison::changed;
    }
    return HudAtlasMutationComparison::unchanged;
}

bool finish_hud_atlas_surface_write(
    SurfaceImage& image, DWORD lock_flags, const RECT& locked_rect,
    HudAtlasMutationSnapshot& snapshot,
    bool& increment_uniqueness) noexcept {
    increment_uniqueness = false;
    if (lock_flags & DDLOCK_READONLY) {
        snapshot = {};
        return false;
    }

    const HudAtlasMutationComparison comparison =
        compare_hud_atlas_mutation(image, snapshot);
    const RECT changed_rect = snapshot.rect;
    snapshot = {};
    image.gpu_captured = false;
    if (comparison == HudAtlasMutationComparison::unchanged) return true;
    const RECT& written = comparison == HudAtlasMutationComparison::changed
        ? changed_rect : locked_rect;
    mark_surface_dirty(image, written);
    advance_surface_generation(image, &written);
    increment_uniqueness = true;
    return false;
}

} // namespace owned
