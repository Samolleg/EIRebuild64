#include "E1Surface.h"
#include "../../external/owned-renderer/Source/renderer.h"
#include <limits>
#include <map>
#include <atomic>
#include <mutex>

namespace {
using Microsoft::WRL::ComPtr;
struct Entry {
    std::shared_ptr<const owned::OwnedVisualSurface> value;
    ComPtr<ID3D11Texture2D> texture;
    std::shared_ptr<owned::SurfaceImage> image;
};
// Source and c56 consumers run on the same render thread. The resolver rejects
// another device; it never opens a shared handle or silently copies between devices.
struct Registry { std::mutex lock; std::map<std::uint64_t, Entry> values; };
// Process-lifetime bookkeeping survives global game-manager destruction. Each
// surface owner still erases its entry and releases its GPU resource normally.
Registry& registry() { static Registry* value = new Registry; return *value; }
std::map<std::uint64_t, Entry>& entries() { return registry().values; }
std::atomic<std::uint64_t> nextId{0};
std::mutex& guard() { return registry().lock; }
bool describe(ID3D11Texture2D* texture, owned::OwnedVisualSurface& value) noexcept {
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
    if (!d.Width || !d.Height || d.ArraySize != 1 || d.SampleDesc.Count != 1 || !d.MipLevels) return false;
    value.width = d.Width; value.height = d.Height; value.format.dwSize = sizeof(value.format);
    auto& f = value.format;
    f.dwFlags = DDPF_RGB;
    switch (d.Format) {
    case DXGI_FORMAT_BC1_UNORM: f.dwFlags = DDPF_FOURCC; f.dwFourCC = MAKEFOURCC('D','X','T','1'); value.pitch = ((d.Width+3)/4)*8; break;
    case DXGI_FORMAT_BC2_UNORM: f.dwFlags = DDPF_FOURCC; f.dwFourCC = MAKEFOURCC('D','X','T','3'); value.pitch = ((d.Width+3)/4)*16; break;
    case DXGI_FORMAT_BC3_UNORM: f.dwFlags = DDPF_FOURCC; f.dwFourCC = MAKEFOURCC('D','X','T','5'); value.pitch = ((d.Width+3)/4)*16; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        f.dwRGBBitCount = 32; f.dwRBitMask = 0xff0000; f.dwGBitMask = 0x00ff00; f.dwBBitMask = 0x0000ff;
        f.dwRGBAlphaBitMask = 0xff000000; value.pitch = d.Width*4; break;
    default: return false;
    }
    if (f.dwRGBAlphaBitMask) f.dwFlags |= DDPF_ALPHAPIXELS;
    // Alpha-bearing sources remain conservative without CPU readback. No
    // made-up alpha cut mask or assertion that unknown texels are opaque.
    return true;
}
}
namespace OwnedRender {
E1Surface::E1Surface() : id(++nextId) { registry(); if (!id) id = ++nextId; }
E1Surface::~E1Surface() { BeginWrite(); }
void E1Surface::BeginWrite() noexcept { const std::lock_guard<std::mutex> lock(guard()); entries().erase(id); }
bool E1Surface::Complete(ID3D11Texture2D* texture, bool alphaFullyOpaque) noexcept try {
    BeginWrite();
    auto value = std::make_shared<owned::OwnedVisualSurface>();
    if (!describe(texture, *value)) return false;
    value->alpha_fully_opaque = alphaFullyOpaque;
    if (++generation == 0) ++generation;
    value->id = id; value->generation = generation;
    Entry entry; entry.value = std::move(value); entry.texture = texture;
    const std::lock_guard<std::mutex> lock(guard());
    entries().emplace(id, std::move(entry));
    return true;
} catch (...) { BeginWrite(); return false; }
bool E1Surface::CompleteCpu(const owned::SurfaceImage& source, bool alphaFullyOpaque) noexcept try {
    BeginWrite();
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT blockBytes = 0;
    if (!source.width || !source.height || source.width > UINT((std::numeric_limits<LONG>::max)()) ||
        source.height > UINT((std::numeric_limits<LONG>::max)()) || source.pitch <= 0 || source.cpu_released ||
        !owned::surface_dxgi_format(source.format, format, blockBytes)) return false;
    if (!blockBytes && source.format.dwRGBBitCount != 16 &&
        source.format.dwRGBBitCount != 24 && source.format.dwRGBBitCount != 32) return false;
    const std::uint64_t rows = blockBytes ? (std::uint64_t(source.height) + 3) / 4 : source.height;
    const std::uint64_t rowBytes = blockBytes ? ((std::uint64_t(source.width) + 3) / 4) * blockBytes :
        (std::uint64_t(source.width) * source.format.dwRGBBitCount + 7) / 8;
    if (!rowBytes || rowBytes > std::uint64_t(source.pitch) ||
        rows > (std::numeric_limits<std::size_t>::max)() / std::size_t(source.pitch) ||
        source.pixels.size() < rows * std::size_t(source.pitch)) return false;
    auto image = std::shared_ptr<owned::SurfaceImage>(new owned::SurfaceImage,
        [](owned::SurfaceImage* value) {
            owned::release_surface_memory_accounting(*value); delete value;
        });
    image->width = source.width; image->height = source.height;
    image->pitch = source.pitch; image->format = source.format;
    image->pixels = source.pixels;
    image->has_color_key = source.has_color_key; image->color_key = source.color_key;
    image->static_mip_hint = source.static_mip_hint; image->cpu_pinned = true;
    auto value = std::make_shared<owned::OwnedVisualSurface>();
    value->width = image->width; value->height = image->height;
    value->pitch = image->pitch; value->format = image->format;
    value->has_color_key = image->has_color_key; value->color_key = image->color_key;
    value->static_mip_hint = image->static_mip_hint;
    value->alpha_fully_opaque = alphaFullyOpaque;
    if (++generation == 0) ++generation;
    image->id = value->id = id; image->generation = value->generation = generation;
    owned::update_surface_memory_accounting(*image);
    Entry entry; entry.value = std::move(value); entry.image = std::move(image);
    const std::lock_guard<std::mutex> lock(guard());
    entries().emplace(id, std::move(entry));
    return true;
} catch (...) { BeginWrite(); return false; }
HRESULT E1CopyCpuSurface(std::uint64_t id, owned::SurfaceImage& image) noexcept try {
    image = {};
    const std::lock_guard<std::mutex> lock(guard());
    const auto found = entries().find(id);
    if (found == entries().end()) return E_HANDLE;
    if (!found->second.image) return S_FALSE;
    const auto& source = *found->second.image;
    image.id = source.id; image.generation = source.generation;
    image.width = source.width; image.height = source.height;
    image.pitch = source.pitch; image.format = source.format;
    image.pixels = source.pixels;
    image.has_color_key = source.has_color_key; image.color_key = source.color_key;
    image.static_mip_hint = source.static_mip_hint; image.cpu_pinned = true;
    return S_OK;
} catch (...) { image = {}; return E_OUTOFMEMORY; }
std::shared_ptr<const owned::OwnedVisualSurface> E1SurfaceValue(std::uint64_t id) noexcept {
    const std::lock_guard<std::mutex> lock(guard());
    const auto found = entries().find(id);
    return found == entries().end() ? nullptr : found->second.value;
}
HRESULT E1ResolveSurface(void*, ID3D11Device* device, std::uint64_t id,
    std::uint64_t generation, ComPtr<ID3D11Texture2D>& texture, owned::SurfaceImage*& image) noexcept {
    image = nullptr; // Only the CPU publication owns a mutable upload home.
    texture.Reset();
    if (!id || !generation) return E_INVALIDARG;
    const std::lock_guard<std::mutex> lock(guard());
    const auto found = entries().find(id);
    if (found == entries().end() || found->second.value->generation != generation) return E_HANDLE;
    if (!device) {
        if (!found->second.image) return E_PENDING;
        image = found->second.image.get();
        return S_OK;
    }
    if (!found->second.texture) return E_INVALIDARG;
    ComPtr<ID3D11Device> actual; found->second.texture->GetDevice(&actual);
    if (actual.Get() != device) return E_INVALIDARG;
    texture = found->second.texture; return S_OK;
}
// Keep direct host readback callers on their existing surface API.
HRESULT E1ResolveSurface(void* context, ID3D11Device* device, std::uint64_t id,
    std::uint64_t generation, ComPtr<ID3D11Texture2D>& texture) noexcept {
    owned::SurfaceImage* image = nullptr;
    return E1ResolveSurface(context, device, id, generation, texture, image);
}

}
