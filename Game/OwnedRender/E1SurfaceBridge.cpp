#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "E1SurfaceBridge.h"
#include "E1Surface.h"
#include <cstring>
#include <limits>

namespace OwnedRender {
HRESULT E1ReadbackSurface(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, E1SurfaceCpuImage& image) noexcept try {
    image = {};
    if (!device || !context || !id) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    context->GetDevice(&contextDevice);
    if (contextDevice.Get() != device) return E_INVALIDARG;
    const auto value = E1SurfaceValue(id);
    if (!value) return E_HANDLE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
    HRESULT hr = E1ResolveSurface(nullptr, device, value->id, value->generation, source);
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    UINT rowBytes = 0, rows = desc.Height;
    switch (desc.Format) {
    case DXGI_FORMAT_BC1_UNORM: rowBytes = ((desc.Width + 3) / 4) * 8; rows = (desc.Height + 3) / 4; break;
    case DXGI_FORMAT_BC2_UNORM: rowBytes = ((desc.Width + 3) / 4) * 16; rows = (desc.Height + 3) / 4; break;
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_B4G4R4A4_UNORM: rowBytes = desc.Width * 2; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM: rowBytes = desc.Width * 4; break;
    default: return DXGI_ERROR_UNSUPPORTED;
    }
    if (!rowBytes || !rows || rowBytes > static_cast<UINT>((std::numeric_limits<LONG>::max)()) ||
        std::size_t(rowBytes) > (std::numeric_limits<std::size_t>::max)() / rows)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING; stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; stagingDesc.MiscFlags = 0;
    stagingDesc.ArraySize = stagingDesc.MipLevels = 1; stagingDesc.SampleDesc.Count = 1;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) return hr;
    std::vector<std::uint8_t> pixels(std::size_t(rowBytes) * rows);
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source.Get(), 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    if (mapped.RowPitch < rowBytes) { context->Unmap(staging.Get(), 0); return E_FAIL; }
    for (UINT y = 0; y < rows; ++y)
        std::memcpy(pixels.data() + std::size_t(y) * rowBytes,
            static_cast<const std::uint8_t*>(mapped.pData) + std::size_t(y) * mapped.RowPitch,
            rowBytes);
    context->Unmap(staging.Get(), 0);
    image.id = value->id; image.width = value->width; image.height = value->height;
    image.pitch = static_cast<LONG>(rowBytes); image.format = desc.Format;
    image.pixels = std::move(pixels); image.generation = value->generation;
    return S_OK;
} catch (...) { image = {}; return E_OUTOFMEMORY; }
}
