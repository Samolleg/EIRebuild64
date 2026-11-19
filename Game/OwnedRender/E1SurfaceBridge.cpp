#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../stdafx.h"
#include "E1SurfaceBridge.h"
#include "E1TextLifetime.h"
#include "E1PerfDiagnostics.h"
#include "E1Surface.h"
#include "../../external/owned-renderer/Source/renderer.h"
#include "../../external/owned-renderer/Source/surface_convert.h"
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

HRESULT E1CaptureNativeTarget(owned::Renderer& renderer, UINT width, UINT height,
    E1SurfaceCpuImage& image) noexcept try {
    image = {};
    const std::size_t rowBytes = std::size_t(width) * sizeof(DWORD);
    if (!width || !height || rowBytes > (std::numeric_limits<LONG>::max)() ||
        rowBytes > (std::numeric_limits<std::size_t>::max)() / height)
        return E_INVALIDARG;
    std::vector<std::uint8_t> captured;
    UINT sourceWidth = 0, sourceHeight = 0;
    const HRESULT hr = renderer.read_embedded_d3d12_output(captured, sourceWidth, sourceHeight);
    if (hr != S_OK) return hr;
    // Native readback validates the complete, tightly packed BGRA allocation.
    if (sourceWidth == width && sourceHeight == height) {
        image.pixels = std::move(captured);
    } else {
        DirectX::Image source{};
        source.width = sourceWidth; source.height = sourceHeight;
        source.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        source.rowPitch = std::size_t(sourceWidth) * sizeof(DWORD);
        source.slicePitch = captured.size(); source.pixels = captured.data();
        DirectX::ScratchImage resized;
        const HRESULT scaled = DirectX::Resize(source, width, height,
            DirectX::TEX_FILTER_POINT | DirectX::TEX_FILTER_FORCE_NON_WIC, resized);
        if (FAILED(scaled)) return scaled;
        const auto* pixels = resized.GetImage(0, 0, 0);
        if (!pixels || pixels->rowPitch < rowBytes) return E_FAIL;
        image.pixels.resize(rowBytes * height);
        for (UINT y = 0; y < height; ++y)
            std::memcpy(image.pixels.data() + std::size_t(y) * rowBytes,
                pixels->pixels + std::size_t(y) * pixels->rowPitch, rowBytes);
    }
    image.width = width; image.height = height;
    image.pitch = static_cast<LONG>(rowBytes); image.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return S_OK;
} catch (...) { image = {}; return E_OUTOFMEMORY; }

HRESULT E1ReadbackSurface(ID3D11Device* device, ID3D11DeviceContext* context,
    std::uint64_t id, E1SurfaceCpuImage& image) noexcept try {
    static E1PerfCounter counter{"ui_readback"};
    E1PerfScope timing(counter);
    image = {};
    if (!device && !context) {
        owned::SurfaceImage cpu;
        const HRESULT copied = E1CopyCpuSurface(id, cpu);
        if (copied != S_OK) return copied == S_FALSE ? E_INVALIDARG : copied;
        UINT blockBytes = 0;
        if (!owned::surface_dxgi_format(cpu.format, image.format, blockBytes)) return DXGI_ERROR_UNSUPPORTED;
        // Match GPU readback: RGB masks become BGRA, compressed blocks stay
        // encoded, and publication color keys must not rewrite the raw pixels.
        cpu.has_color_key = false;
        RECT rect{};
        surface_convert::Prepared prepared;
        const HRESULT converted = owned::prepare_surface_upload(cpu, true, blockBytes, rect, prepared);
        if (FAILED(converted)) return converted;
        const std::uint64_t rowBytes = blockBytes ? ((std::uint64_t(cpu.width) + 3) / 4) * blockBytes :
            std::uint64_t(cpu.width) * 4;
        const UINT rows = blockBytes ? (cpu.height + 3) / 4 : cpu.height;
        if (rowBytes > UINT((std::numeric_limits<LONG>::max)()) || rowBytes > prepared.pitch ||
            rowBytes > (std::numeric_limits<std::size_t>::max)() / rows) return E_INVALIDARG;
        image.id = cpu.id; image.generation = cpu.generation;
        image.width = cpu.width; image.height = cpu.height; image.pitch = LONG(rowBytes);
        image.pixels.resize(std::size_t(rowBytes) * rows);
        for (UINT y = 0; y < rows; ++y)
            std::memcpy(image.pixels.data() + std::size_t(y) * std::size_t(rowBytes),
                static_cast<const std::uint8_t*>(prepared.data) + std::size_t(y) * prepared.pitch,
                std::size_t(rowBytes));
        return S_OK;
    }
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
    if (!device && !context) {
        const HRESULT copied = E1CopyCpuSurface(id, image);
        return copied == S_FALSE ? E_INVALIDARG : copied;
    }
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
            static E1PerfCounter counter{"surface_cache_copy"};
            E1PerfScope timing(counter, found->second.image.pixels.size());
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
    if (value && !device && !context) {
        struct CpuEntry {
            std::uint64_t used = 0;
            std::shared_ptr<owned::SurfaceImage> image;
        };
        // Render-thread cache, like the D3D11 snapshot cache below. Keep the
        // upload home across particle draws until this publication changes.
        static auto* cpuCache = new std::map<std::uint64_t, CpuEntry>;
        static std::uint64_t cpuSerial = 0;
        const auto found = cpuCache->find(id);
        if (found != cpuCache->end() && found->second.image->generation == value->generation) {
            found->second.used = ++cpuSerial;
            image = found->second.image;
            return S_OK;
        }
        auto cpu = std::shared_ptr<owned::SurfaceImage>(new owned::SurfaceImage,
            [](owned::SurfaceImage* value) {
                owned::release_surface_memory_accounting(*value); delete value;
            });
        const HRESULT copied = E1CopyCpuSurface(id, *cpu);
        if (copied != S_OK) return copied == S_FALSE ? E_INVALIDARG : copied;
        owned::update_surface_memory_accounting(*cpu);
        (*cpuCache)[id] = {++cpuSerial, cpu};
        while (cpuCache->size() > 64) {
            auto oldest = cpuCache->begin();
            for (auto it = cpuCache->begin(); it != cpuCache->end(); ++it)
                if (it->second.used < oldest->second.used) oldest = it;
            cpuCache->erase(oldest);
        }
        image = std::move(cpu);
        return S_OK;
    }
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
    // Effects only sample the completed surface. Keep an exact, immutable GPU
    // copy of level 0 instead of synchronously reading it to CPU and uploading
    // those same bytes again. A separate resource preserves this generation
    // even when the host later writes to the published texture in place.
    static E1PerfCounter counter{"particle_surface_snapshot"};
    E1PerfScope timing(counter);
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    context->GetDevice(&contextDevice);
    if (contextDevice.Get() != device) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{}; published->GetDesc(&desc);
    if (desc.Width != value->width || desc.Height != value->height ||
        desc.ArraySize != 1 || desc.SampleDesc.Count != 1) return E_HANDLE;
    desc.MipLevels = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.CPUAccessFlags = 0; desc.MiscFlags = 0;
    HRESULT copied = device->CreateTexture2D(&desc, nullptr, &next->texture);
    if (FAILED(copied)) return copied;
    copied = device->CreateShaderResourceView(next->texture.Get(), nullptr, &next->srv);
    if (FAILED(copied)) return copied;
    context->CopySubresourceRegion(next->texture.Get(), 0, 0, 0, 0, published.Get(), 0, nullptr);
    next->id = value->id; next->generation = value->generation;
    next->width = value->width; next->height = value->height;
    next->pitch = value->pitch; next->format = value->format;
    next->uploaded_shape = owned::surface_upload_shape(*next);
    next->uploaded_generation = next->generation; next->texture_serial = 1;
    next->gpu_primary = true; next->cpu_released = true; next->dirty_full = false;
    owned::update_surface_memory_accounting(*next);
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
    static E1PerfCounter counter{"ui_blit_total"};
    E1PerfScope timing(counter);
    if (renderer.d3d12_backend()) {
        const auto value = E1SurfaceValue(id);
        if (!value) return E_HANDLE;
        HRESULT textResult = S_OK;
        if (owned::owned_text_consume(renderer, id, value->generation, destination, source, textResult))
            return textResult;
        owned::SurfaceImage image;
        const HRESULT copied = E1CopyCpuSurface(id, image);
        if (copied != S_OK) return copied == S_FALSE ? E_PENDING : copied;
        return renderer.blit_embedded_snapshot(image, destination, source, color_key, interface_space, native_canvas);
    }
    if (!device || !context || !id) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    context->GetDevice(&contextDevice);
    if (contextDevice.Get() != device) return E_INVALIDARG;
    const auto value = E1SurfaceValue(id);
    if (!value) return E_HANDLE;
    HRESULT textResult = S_OK;
    if (owned::owned_text_consume(renderer, id, value->generation, destination, source, textResult))
        return textResult;
    owned::SurfaceImage image;
    HRESULT hr = E1ResolveSurface(nullptr, device, id, value->generation, image.texture);
    if (FAILED(hr)) return hr;
    image.id = value->id; image.generation = value->generation;
    image.width = value->width; image.height = value->height;
    image.pitch = value->pitch; image.format = value->format;
    hr = device->CreateShaderResourceView(image.texture.Get(), nullptr, &image.srv);
    if (FAILED(hr)) return hr;
    // Borrow the published GPU image; the source owner retains its accounting.
    // The existing GPU blit handles clipping, scaling, opaque copies and keys.
    image.gpu_captured = true;
    hr = renderer.blit(image, destination, source, color_key, interface_space, native_canvas);
    if (FAILED(hr)) return hr;
    // Submit before the host can mutate this texture again. Unlike the old CPU
    // snapshot, a borrowed texture cannot safely wait for a later UI flush.
    return renderer.flush_ui();
} catch (...) { return E_FAIL; }
}
