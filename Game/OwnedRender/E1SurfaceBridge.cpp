#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../stdafx.h"
#include "E1SurfaceBridge.h"
#include "E1Surface.h"
#include "../../external/owned-renderer/Source/renderer.h"
#include <DirectXTex.h>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>

namespace OwnedRender {
namespace {
struct CachedSurfaceImage {
    std::uint64_t generation = 0;
    owned::SurfaceImage image;
};
std::mutex& surfaceImageCacheLock() { static auto* value = new std::mutex; return *value; }
std::map<std::uint64_t, CachedSurfaceImage>& surfaceImageCache() {
    static auto* value = new std::map<std::uint64_t, CachedSurfaceImage>;
    return *value;
}
}

HRESULT E1CaptureTarget(ID3D11DeviceContext* context, ID3D11Texture2D* source,
    UINT width, UINT height, DXGI_FORMAT format, E1SurfaceCpuImage& image) noexcept try {
    image = {};
    if (!context || !source || !width || !height ||
        format != DXGI_FORMAT_B8G8R8A8_UNORM) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    DirectX::ScratchImage captured, converted, resized;
    HRESULT hr = DirectX::CaptureTexture(device.Get(), context, source, captured);
    if (FAILED(hr)) return hr;
    const DirectX::Image* pixels = captured.GetImage(0, 0, 0);
    if (!pixels) return E_FAIL;
    if (pixels->format != format) {
        // Raw linear values preserve the zone codes; scene postprocessing would
        // change them. Force the non-WIC conversion to avoid color management.
        hr = DirectX::Convert(*pixels, format, DirectX::TEX_FILTER_FORCE_NON_WIC,
            DirectX::TEX_THRESHOLD_DEFAULT, converted);
        if (FAILED(hr)) return hr;
        pixels = converted.GetImage(0, 0, 0);
    }
    if (pixels->width != width || pixels->height != height) {
        hr = DirectX::Resize(*pixels, width, height,
            DirectX::TEX_FILTER_POINT | DirectX::TEX_FILTER_FORCE_NON_WIC, resized);
        if (FAILED(hr)) return hr;
        pixels = resized.GetImage(0, 0, 0);
    }
    const std::size_t rowBytes = std::size_t(width) * sizeof(DWORD);
    if (rowBytes > (std::numeric_limits<LONG>::max)() ||
        rowBytes > (std::numeric_limits<std::size_t>::max)() / height)
        return E_INVALIDARG;
    image.pixels.resize(rowBytes * height);
    for (UINT y = 0; y < height; ++y)
        std::memcpy(image.pixels.data() + std::size_t(y) * rowBytes,
            pixels->pixels + std::size_t(y) * pixels->rowPitch, rowBytes);
    image.width = width; image.height = height;
    image.pitch = static_cast<LONG>(rowBytes); image.format = format;
    return S_OK;
} catch (...) { image = {}; return E_OUTOFMEMORY; }

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

HRESULT E1SurfaceImage(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, owned::SurfaceImage& image) noexcept try {
    image = {};
    if (!device || !context || !id) return E_INVALIDARG;
    const auto value = E1SurfaceValue(id);
    if (!value) return E_HANDLE;
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    context->GetDevice(&contextDevice);
    if (contextDevice.Get() != device) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> published;
    const HRESULT resolved = E1ResolveSurface(nullptr, device, id, value->generation, published);
    if (FAILED(resolved)) return resolved;
    {
        const std::lock_guard<std::mutex> lock(surfaceImageCacheLock());
        const auto found = surfaceImageCache().find(id);
        if (found != surfaceImageCache().end() &&
            found->second.generation == value->generation) {
            image = found->second.image;
            return S_OK;
        }
    }
    if (!(value->format.dwFlags & (DDPF_RGB | DDPF_FOURCC))) return DXGI_ERROR_UNSUPPORTED;
    E1SurfaceCpuImage pixels;
    const HRESULT read = E1ReadbackSurface(device, context, id, pixels);
    if (FAILED(read)) return read;
    if (pixels.generation != value->generation || pixels.width != value->width ||
        pixels.height != value->height || pixels.format == DXGI_FORMAT_UNKNOWN ||
        pixels.pitch != value->pitch) return E_HANDLE;
    owned::SurfaceImage converted;
    converted.id = value->id;
    converted.generation = value->generation;
    converted.width = value->width;
    converted.height = value->height;
    converted.pitch = value->pitch;
    converted.format = value->format;
    converted.pixels = pixels.pixels;
    {
        const std::lock_guard<std::mutex> lock(surfaceImageCacheLock());
        auto& cache = surfaceImageCache();
        cache[id] = {value->generation, converted};
        // Keep the cache bounded when transient UI surfaces are retired.
        while (cache.size() > 64) cache.erase(cache.begin());
    }
    image = std::move(converted);
    return S_OK;
} catch (...) { image = {}; return E_OUTOFMEMORY; }

HRESULT E1ParticleSurfaceImage(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, std::shared_ptr<owned::SurfaceImage>& image) noexcept try {
    image.reset();
    const auto value = E1SurfaceValue(id);
    if (!value || !device || !context) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> published;
    const HRESULT resolved = E1ResolveSurface(nullptr, device, id, value->generation, published);
    if (FAILED(resolved)) return resolved;
    struct Entry {
        ID3D11Device* device = nullptr;
        std::uint64_t used = 0;
        std::shared_ptr<owned::SurfaceImage> image;
    };
    // Called on the render thread, just like the immediate particle submission.
    static auto* cache = new std::map<std::uint64_t, Entry>;
    static std::uint64_t serial = 0;
    const auto found = cache->find(id);
    if (found != cache->end() && found->second.device == device &&
        found->second.image->generation == value->generation) {
        found->second.used = ++serial;
        image = found->second.image;
        return S_OK;
    }
    auto next = std::shared_ptr<owned::SurfaceImage>(new owned::SurfaceImage,
        [](owned::SurfaceImage* value) {
            owned::release_surface_memory_accounting(*value);
            delete value;
        });
    const HRESULT converted = E1SurfaceImage(device, context, id, *next);
    if (FAILED(converted)) return converted;
    (*cache)[id] = {device, ++serial, next};
    while (cache->size() > 64) {
        auto oldest = cache->begin();
        for (auto it = cache->begin(); it != cache->end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        cache->erase(oldest);
    }
    image = std::move(next);
    return S_OK;
} catch (...) { image.reset(); return E_OUTOFMEMORY; }

HRESULT E1BlitPublishedSurface(owned::Renderer& renderer, ID3D11Device* device,
    ID3D11DeviceContext* context, std::uint64_t id, const RECT& destination,
    const RECT* source, const DDCOLORKEY* color_key, bool interface_space,
    bool native_canvas) noexcept try {
    owned::SurfaceImage image;
    const HRESULT converted = E1SurfaceImage(device, context, id, image);
    if (FAILED(converted)) return converted;
    return renderer.blit(image, destination, source, color_key, interface_space, native_canvas);
} catch (...) { return E_FAIL; }
}
