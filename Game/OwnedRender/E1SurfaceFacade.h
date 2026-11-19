#pragma once

#include "E1RendererFacade.h"
#include "E1SurfaceBridge.h"
#include <type_traits>

namespace OwnedRender {

// Submit one completed interface surface through the facade's active c56
// frame. Keeping the source device/context separate from the renderer device
// lets the bridge read the host surface back before c56 consumes its snapshot.
template<class Backend>
inline HRESULT E1BlitPublishedSurface(RendererFacadeCore<Backend>& facade,
    ID3D11Device* sourceDevice, ID3D11DeviceContext* sourceContext,
    std::uint64_t surfaceId, const RECT& destination, const RECT* source,
    const DDCOLORKEY* colorKey, bool interfaceSpace, bool nativeCanvas = true) noexcept {
    if (!facade.FrameActive()) return E_UNEXPECTED;
    return facade.VisitKeepingBlits([&](Backend& backend) {
        if constexpr (std::is_same_v<Backend, owned::Renderer>) {
            return E1BlitPublishedSurface(backend, sourceDevice, sourceContext,
                surfaceId, destination, source, colorKey, interfaceSpace, nativeCanvas);
        } else {
            return E1BlitPublishedSurface(backend.renderer, sourceDevice, sourceContext,
                surfaceId, destination, source, colorKey, interfaceSpace, nativeCanvas);
        }
    });
}

} // namespace OwnedRender
