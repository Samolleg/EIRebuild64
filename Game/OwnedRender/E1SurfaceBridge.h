#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace owned { struct OwnedVisualSurface; struct SurfaceImage; class Renderer; }
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
// Capture the raw c56 target, resolving MSAA and converting/scaling to the host
// surface without tone mapping or interpolation of color-coded hit-test masks.
HRESULT E1CaptureTarget(ID3D11DeviceContext* context, ID3D11Texture2D* source,
    UINT width, UINT height, DXGI_FORMAT format, E1SurfaceCpuImage& image) noexcept;
// Read the native output (or private hit-test target) and point-scale BGRA
// pixels to the source surface dimensions, preserving lossless mask codes.
HRESULT E1CaptureNativeTarget(owned::Renderer& renderer, UINT width, UINT height,
    E1SurfaceCpuImage& image) noexcept;
// Read one completed published surface into the CPU image consumed by c56 blit.
HRESULT E1ReadbackSurface(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, E1SurfaceCpuImage& image) noexcept;
// Convert a completed panel surface into the immutable CPU snapshot consumed
// by c56 blit. Repeated reads of the same generation reuse the cached snapshot.
HRESULT E1SurfaceImage(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, owned::SurfaceImage& image) noexcept;
// Retain particle upload state across draws; generation/device changes retire it.
HRESULT E1ParticleSurfaceImage(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, std::shared_ptr<owned::SurfaceImage>& image) noexcept;
HRESULT E1BlitPublishedSurface(owned::Renderer& renderer, ID3D11Device* device,
    ID3D11DeviceContext* context, std::uint64_t id, const RECT& destination,
    const RECT* source, const DDCOLORKEY* color_key, bool interface_space,
    bool native_canvas = true) noexcept;
}
