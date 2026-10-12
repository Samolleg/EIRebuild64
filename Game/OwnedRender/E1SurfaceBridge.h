#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace owned { struct OwnedVisualSurface; struct SurfaceImage; }
namespace OwnedRender {
struct E1SurfaceCpuImage {
    std::uint64_t id = 0, generation = 0;
    UINT width = 0, height = 0;
    LONG pitch = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<std::uint8_t> pixels;
};
std::shared_ptr<const owned::OwnedVisualSurface> E1SurfaceValue(std::uint64_t id) noexcept;
HRESULT E1ResolveSurface(void*, ID3D11Device* device, std::uint64_t id,
    std::uint64_t generation, Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept;
// Read one completed published surface into the CPU image consumed by c56 blit.
HRESULT E1ReadbackSurface(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, E1SurfaceCpuImage& image) noexcept;
}
