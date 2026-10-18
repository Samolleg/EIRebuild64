#pragma once
#include "engine_bridge.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace owned { struct OwnedVisualSurface; struct SurfaceImage; }
namespace OwnedRender {
// Render-thread owner of one completed GPU surface. IDs never encode addresses.
class E1Surface {
    std::uint64_t id = 0, generation = 0;
public:
    E1Surface();
    ~E1Surface();
    E1Surface(const E1Surface&) = delete;
    E1Surface& operator=(const E1Surface&) = delete;
    std::uint64_t Id() const noexcept { return id; }
    void BeginWrite() noexcept;
    bool Complete(ID3D11Texture2D* texture) noexcept;
};
std::shared_ptr<const owned::OwnedVisualSurface> E1SurfaceValue(std::uint64_t id) noexcept;
HRESULT E1ResolveSurface(void*, ID3D11Device* device, std::uint64_t id,
    std::uint64_t generation, Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture, owned::SurfaceImage*& image) noexcept;
HRESULT E1ResolveSurface(void*, ID3D11Device* device, std::uint64_t id,
    std::uint64_t generation, Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept;

}
