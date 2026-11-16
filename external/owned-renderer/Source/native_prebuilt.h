#pragma once
// Prebuilt neighbour geometry pages and textures (native_prebuilt.cpp): the
// layout get_native_mesh uploads, built on a stream worker with only an
// ID3D11Device. Shared by the renderer DLL and the x64 render host.
#include "neighbour_maps.h"

#include <d3d11.h>
#include <cstdint>

namespace owned {

// One mesh component in the GPU geometry page (get_native_mesh's layout).
struct NativeGpuComponent {
    std::uint32_t position;
    std::uint32_t normal;
    std::uint32_t texture;
    std::uint32_t padding;
};

constexpr UINT kNativeRawElementLimit=
 1u<<D3D11_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP;
constexpr UINT kNativeRawBufferLimit=kNativeRawElementLimit*sizeof(UINT);

// A liquid vertex's type bits (NativeMesh::liquid_types): 1 water, 2 lava,
// 4 swamp (native_liquid_type_mask: the program choice) and
// native_liquid_flowing (fall tessellation may displace it).
constexpr std::uint8_t native_liquid_type_mask=7u,native_liquid_flowing=8u;
std::uint8_t native_liquid_vertex_types(const NativeTerrainVertex& vertex) noexcept;

} // namespace owned
