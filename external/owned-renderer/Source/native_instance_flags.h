#pragma once

#include <cstdint>

#include "contact_blend.h"
#include "shadow_foliage.h"

namespace owned {

// Native instance flags (NativeInstanceGpu::flags / HLSL nativeFlags).
// Every field owns its bits exclusively; the static_asserts below keep it so.
//
//   bit(s)  mask        meaning
//   0       0x00000001  UPDATE_EXTENTS request (view only; never read by shaders)
//   1       0x00000002  unit (a creature or character: native_unit_flag; snow takes a sheen only)
//   2       0x00000004  use default diffuse/specular colours
//   3       0x00000008  specular enable
//   4       0x00000010  owned preview specular RGB (native_preview_specular_rgb_flag)
//   5       0x00000020  neighbour-map geometry (native_neighbour_flag)
//   6       0x00000040  retained part-node component layout
//   7       0x00000080  ground contact (native_ground_contact_flag, contact_blend.h)
//   8       0x00000100  foliage part (native_foliage_part_flag, shadow_foliage.h)
//   9       0x00000200  part of a swaying plant (native_sway_flag)
//   10-15   0x0000fc00  sway trunk top, 1/4 unit
//   16-21   0x003f0000  sway trunk radius, 1/16 unit
//   22-27   0x0fc00000  sway plant height, 1/4 unit
//   28-31   0xf0000000  sway part node distance from the root pose node (0-15)
//
// The shader reads the sway fields with the literal shifts above and gets the
// neighbour, contact and preview masks substituted from these constants.
#ifdef EID3D11_EMBEDDED
// Transient host triangles carry homogeneous coordinates; retail FIG keeps W=1.
constexpr std::uint32_t native_embedded_homogeneous_flag = 0x2u;
#endif
constexpr std::uint32_t native_neighbour_flag = 0x20u;
constexpr std::uint32_t native_unit_flag = 0x2u;
constexpr std::uint32_t native_preview_specular_rgb_flag = 0x10u;
constexpr std::uint32_t native_sway_flag = 0x200u;
constexpr std::uint32_t native_sway_shape_mask = 0x0ffffc00u;
constexpr unsigned native_sway_root_shift = 28;
constexpr std::uint32_t native_sway_root_mask = 0xfu << native_sway_root_shift;
constexpr std::uint32_t native_sway_root_max = 15u;

constexpr std::uint32_t native_sway_root_bits(std::uint32_t offset) noexcept {
    return (offset & native_sway_root_max) << native_sway_root_shift;
}
constexpr std::uint32_t native_sway_root_offset(std::uint32_t flags) noexcept {
    return flags >> native_sway_root_shift;
}

// Single-bit flags: their sum equals their union only when no two share a bit.
#ifdef EID3D11_EMBEDDED
static_assert((native_embedded_homogeneous_flag & (0x1u | 0x4u | 0x8u |
    native_preview_specular_rgb_flag | 0x40u | native_neighbour_flag | native_ground_contact_flag |
    native_foliage_part_flag | native_sway_flag | native_sway_shape_mask | native_sway_root_mask)) == 0,
    "embedded homogeneous flag overlaps native fields");
#endif
constexpr std::uint32_t native_low_instance_flags =
    0x1u | native_unit_flag | 0x4u | 0x8u | native_preview_specular_rgb_flag | 0x40u | native_neighbour_flag | native_ground_contact_flag |
    native_foliage_part_flag | native_sway_flag;
static_assert(0x1u + native_unit_flag + 0x4u + 0x8u + native_preview_specular_rgb_flag + 0x40u + native_neighbour_flag + native_ground_contact_flag +
    native_foliage_part_flag + native_sway_flag == native_low_instance_flags,
    "single-bit instance flags overlap");
static_assert((native_low_instance_flags & (native_sway_shape_mask | native_sway_root_mask)) == 0,
    "instance flag overlaps the sway fields");
static_assert((native_sway_shape_mask & native_sway_root_mask) == 0, "sway fields overlap");

} // namespace owned
