#include "renderer.h"
#include "texture_mips.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <vector>

namespace owned {
namespace {
#ifdef EID3D11_TESTING
bool fail_next_surface_srv_creation = false;
#endif

struct ChannelDecoder {
    std::uint32_t mask = 0;
    std::uint32_t maximum = 0;
    unsigned shift = 0;
    std::uint8_t fallback = 0;
    bool use_lookup = false;
    std::array<std::uint8_t, 256> lookup{};

    std::uint8_t decode(std::uint32_t raw) const noexcept {
        if (!mask) return fallback;
        const std::uint32_t value = (raw & mask) >> shift;
        if (use_lookup) return lookup[value];
        return static_cast<std::uint8_t>(value * 255u / maximum);
    }
};

ChannelDecoder make_channel_decoder(std::uint32_t mask,
                                    std::uint8_t fallback) noexcept {
    ChannelDecoder decoder{};
    decoder.mask = mask;
    decoder.fallback = fallback;
    if (!mask) return decoder;
    while (((mask >> decoder.shift) & 1u) == 0u) ++decoder.shift;
    decoder.maximum = mask >> decoder.shift;
    decoder.use_lookup = decoder.maximum <= 255u;
    if (decoder.use_lookup) {
        for (std::uint32_t value = 0; value <= decoder.maximum; ++value)
            decoder.lookup[value] = static_cast<std::uint8_t>(
                value * 255u / decoder.maximum);
    }
    return decoder;
}

template <UINT SourceBytes>
std::uint32_t load_pixel(const std::uint8_t* source) noexcept {
    if constexpr (SourceBytes == 1) {
        return source[0];
    } else if constexpr (SourceBytes == 2) {
        return static_cast<std::uint32_t>(source[0]) |
            (static_cast<std::uint32_t>(source[1]) << 8);
    } else if constexpr (SourceBytes == 3) {
        return static_cast<std::uint32_t>(source[0]) |
            (static_cast<std::uint32_t>(source[1]) << 8) |
            (static_cast<std::uint32_t>(source[2]) << 16);
    } else {
        std::uint32_t raw = 0;
        std::memcpy(&raw, source, sizeof(raw));
        return raw;
    }
}

template <UINT SourceBytes, bool HasAlpha, bool HasColorKey>
void convert_pixels(const SurfaceImage& image, const RECT& rect, UINT width,
                    UINT height, const ChannelDecoder& red,
                    const ChannelDecoder& green, const ChannelDecoder& blue,
                    const ChannelDecoder& alpha, std::uint32_t* destination) noexcept {
    for (UINT y = 0; y < height; ++y) {
        const auto* source = image.pixels.data() +
            static_cast<std::size_t>(rect.top + static_cast<LONG>(y)) * image.pitch +
            static_cast<std::size_t>(rect.left) * SourceBytes;
        auto* row = destination + static_cast<std::size_t>(y) * width;
        for (UINT x = 0; x < width; ++x, source += SourceBytes) {
            const std::uint32_t raw = load_pixel<SourceBytes>(source);
            std::uint8_t a = HasAlpha ? alpha.decode(raw) : 255u;
            if constexpr (HasColorKey) {
                if (raw >= image.color_key.dwColorSpaceLowValue &&
                    raw <= image.color_key.dwColorSpaceHighValue) a = 0;
            }
            row[x] = (static_cast<std::uint32_t>(a) << 24) |
                (static_cast<std::uint32_t>(red.decode(raw)) << 16) |
                (static_cast<std::uint32_t>(green.decode(raw)) << 8) |
                blue.decode(raw);
        }
    }
}

template <bool HasAlpha, bool HasColorKey>
void convert_strided_pixels(const SurfaceImage& image, const RECT& rect,
                            UINT width, UINT height, UINT source_bytes,
                            const ChannelDecoder& red,
                            const ChannelDecoder& green,
                            const ChannelDecoder& blue,
                            const ChannelDecoder& alpha,
                            std::uint32_t* destination) noexcept {
    for (UINT y = 0; y < height; ++y) {
        const auto* source = image.pixels.data() +
            static_cast<std::size_t>(rect.top + static_cast<LONG>(y)) * image.pitch +
            static_cast<std::size_t>(rect.left) * source_bytes;
        auto* row = destination + static_cast<std::size_t>(y) * width;
        for (UINT x = 0; x < width; ++x, source += source_bytes) {
            const std::uint32_t raw = load_pixel<4>(source);
            std::uint8_t a = HasAlpha ? alpha.decode(raw) : 255u;
            if constexpr (HasColorKey) {
                if (raw >= image.color_key.dwColorSpaceLowValue &&
                    raw <= image.color_key.dwColorSpaceHighValue) a = 0;
            }
            row[x] = (static_cast<std::uint32_t>(a) << 24) |
                (static_cast<std::uint32_t>(red.decode(raw)) << 16) |
                (static_cast<std::uint32_t>(green.decode(raw)) << 8) |
                blue.decode(raw);
        }
    }
}

template <UINT SourceBytes, bool HasAlpha, bool HasColorKey>
void convert_standard_bgr(const SurfaceImage& image, const RECT& rect,
                          UINT width, UINT height,
                          std::uint32_t* destination) noexcept {
    for (UINT y = 0; y < height; ++y) {
        const auto* source = image.pixels.data() +
            static_cast<std::size_t>(rect.top + static_cast<LONG>(y)) * image.pitch +
            static_cast<std::size_t>(rect.left) * SourceBytes;
        auto* row = destination + static_cast<std::size_t>(y) * width;
        for (UINT x = 0; x < width; ++x, source += SourceBytes) {
            const std::uint32_t raw = load_pixel<SourceBytes>(source);
            std::uint32_t alpha = HasAlpha ? (raw & 0xff000000u) : 0xff000000u;
            if constexpr (HasColorKey) {
                if (raw >= image.color_key.dwColorSpaceLowValue &&
                    raw <= image.color_key.dwColorSpaceHighValue) alpha = 0;
            }
            row[x] = alpha | (raw & 0x00ffffffu);
        }
    }
}

template <UINT SourceBytes, bool HasAlpha>
void dispatch_standard_bgr(const SurfaceImage& image, const RECT& rect,
                           UINT width, UINT height,
                           std::uint32_t* destination) noexcept {
    if (image.has_color_key) {
        convert_standard_bgr<SourceBytes, HasAlpha, true>(
            image, rect, width, height, destination);
    } else {
        convert_standard_bgr<SourceBytes, HasAlpha, false>(
            image, rect, width, height, destination);
    }
}

template <UINT SourceBytes, bool HasAlpha>
void dispatch_keyed_conversion(const SurfaceImage& image, const RECT& rect,
                               UINT width, UINT height,
                               const ChannelDecoder& red,
                               const ChannelDecoder& green,
                               const ChannelDecoder& blue,
                               const ChannelDecoder& alpha,
                               std::uint32_t* destination) noexcept {
    if (image.has_color_key) {
        convert_pixels<SourceBytes, HasAlpha, true>(
            image, rect, width, height, red, green, blue, alpha, destination);
    } else {
        convert_pixels<SourceBytes, HasAlpha, false>(
            image, rect, width, height, red, green, blue, alpha, destination);
    }
}

template <UINT SourceBytes>
void dispatch_alpha_conversion(const SurfaceImage& image, const RECT& rect,
                               UINT width, UINT height,
                               const ChannelDecoder& red,
                               const ChannelDecoder& green,
                               const ChannelDecoder& blue,
                               const ChannelDecoder& alpha,
                               std::uint32_t* destination) noexcept {
    if (image.format.dwRGBAlphaBitMask) {
        dispatch_keyed_conversion<SourceBytes, true>(
            image, rect, width, height, red, green, blue, alpha, destination);
    } else {
        dispatch_keyed_conversion<SourceBytes, false>(
            image, rect, width, height, red, green, blue, alpha, destination);
    }
}

} // namespace

// BC2/BC3 retain the encoded DXT2/DXT4 premultiplied values as supplied.
// Format selection does not unpremultiply texels or alter the caller's blend state.
bool surface_dxgi_format(const DDPIXELFORMAT& format, DXGI_FORMAT& output,
                         UINT& block_bytes) noexcept {
    block_bytes = 0;
    if (format.dwFlags & DDPF_FOURCC) {
        if (format.dwFourCC == MAKEFOURCC('D', 'X', 'T', '1')) {
            output = DXGI_FORMAT_BC1_UNORM;
            block_bytes = 8;
            return true;
        }
        if (format.dwFourCC == MAKEFOURCC('D', 'X', 'T', '2') ||
            format.dwFourCC == MAKEFOURCC('D', 'X', 'T', '3')) {
            output = DXGI_FORMAT_BC2_UNORM;
            block_bytes = 16;
            return true;
        }
        if (format.dwFourCC == MAKEFOURCC('D', 'X', 'T', '4') ||
            format.dwFourCC == MAKEFOURCC('D', 'X', 'T', '5')) {
            output = DXGI_FORMAT_BC3_UNORM;
            block_bytes = 16;
            return true;
        }
        return false;
    }
    if (!(format.dwFlags & DDPF_RGB) || !format.dwRGBBitCount) return false;
    output = DXGI_FORMAT_B8G8R8A8_UNORM;
    return true;
}

bool surface_pixels_round_trip_exact(const SurfaceImage& image) noexcept {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT block_bytes = 0;
    if (!surface_dxgi_format(image.format, format, block_bytes)) return false;
    if (block_bytes) return true;
    constexpr DWORD palette = DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 |
        DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8 | DDPF_PALETTEINDEXEDTO8;
    const DWORD bits = image.format.dwRGBBitCount;
    if ((image.format.dwFlags & palette) || (bits != 16 && bits != 24 && bits != 32))
        return false;
    const DWORD alpha = image.format.dwRGBAlphaBitMask;
    // A key match stores alpha 0, which would overwrite a real alpha channel.
    if (image.has_color_key && alpha) return false;
    const DWORD masks[4] = {image.format.dwRBitMask, image.format.dwGBitMask,
                            image.format.dwBBitMask, alpha};
    DWORD covered = 0;
    for (const DWORD mask : masks) {
        if (!mask) { if (mask != alpha) return false; continue; }
        if (covered & mask) return false;
        covered |= mask;
        DWORD shift = 0;
        while (((mask >> shift) & 1u) == 0u) ++shift;
        const DWORD maximum = mask >> shift;
        // Contiguous and at most 8 bits: floor-decode then round-pack is exact.
        if ((maximum & (maximum + 1u)) != 0u || maximum > 255u) return false;
    }
    // Padding bits have no GPU home; keep such surfaces CPU-resident.
    const DWORD all = bits == 32 ? 0xffffffffu : ((1u << bits) - 1u);
    return covered == all;
}

namespace {

bool matching_texture(ID3D11Texture2D* texture, UINT width, UINT height,
                      DXGI_FORMAT format, bool mips) noexcept {
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    return description.Width == width && description.Height == height &&
        description.MipLevels == (mips?full_mip_count(width,height):1u) && description.ArraySize == 1 &&
        description.Format == format && description.SampleDesc.Count == 1;
}

RECT full_rect(const SurfaceImage& image) noexcept {
    return {0, 0, static_cast<LONG>(image.width), static_cast<LONG>(image.height)};
}

RECT normalize_dirty_rect(const SurfaceImage& image, bool force_full,
                          UINT block_bytes) noexcept {
    RECT rect = force_full || image.dirty_full || !image.dirty_rect_valid
        ? full_rect(image) : image.dirty_rect;
    rect.left = std::clamp<LONG>(rect.left, 0, static_cast<LONG>(image.width));
    rect.top = std::clamp<LONG>(rect.top, 0, static_cast<LONG>(image.height));
    rect.right = std::clamp<LONG>(rect.right, rect.left, static_cast<LONG>(image.width));
    rect.bottom = std::clamp<LONG>(rect.bottom, rect.top, static_cast<LONG>(image.height));
    if (rect.right <= rect.left || rect.bottom <= rect.top) return full_rect(image);
    if (block_bytes) {
        rect.left &= ~3L;
        rect.top &= ~3L;
        rect.right = std::min<LONG>(image.width, (rect.right + 3L) & ~3L);
        rect.bottom = std::min<LONG>(image.height, (rect.bottom + 3L) & ~3L);
    }
    return rect;
}

bool is_full_rect(const SurfaceImage& image, const RECT& rect) noexcept {
    return rect.left == 0 && rect.top == 0 &&
        rect.right == static_cast<LONG>(image.width) &&
        rect.bottom == static_cast<LONG>(image.height);
}

struct PreparedUpload {
    const void* data = nullptr;
    UINT pitch = 0;
    std::uint64_t bytes = 0;
    std::vector<std::uint32_t> converted;
};

HRESULT prepare_upload(const SurfaceImage& image, const RECT& rect,
                       UINT block_bytes, PreparedUpload& output) {
    const UINT width = static_cast<UINT>(rect.right - rect.left);
    const UINT height = static_cast<UINT>(rect.bottom - rect.top);
    if (block_bytes) {
        output.data = image.pixels.data() +
            static_cast<std::size_t>(rect.top / 4) * image.pitch +
            static_cast<std::size_t>(rect.left / 4) * block_bytes;
        output.pitch = static_cast<UINT>(image.pitch);
        output.bytes = static_cast<std::uint64_t>((width + 3) / 4) *
            ((height + 3) / 4) * block_bytes;
        return S_OK;
    }

    const UINT source_bytes = std::max<UINT>(1, image.format.dwRGBBitCount / 8);
    const bool direct_bgra = source_bytes == 4 && !image.has_color_key &&
        image.format.dwRBitMask == 0x00ff0000 &&
        image.format.dwGBitMask == 0x0000ff00 &&
        image.format.dwBBitMask == 0x000000ff &&
        image.format.dwRGBAlphaBitMask == 0xff000000;
    output.bytes = static_cast<std::uint64_t>(width) * height * 4u;
    if (direct_bgra) {
        output.data = image.pixels.data() +
            static_cast<std::size_t>(rect.top) * image.pitch +
            static_cast<std::size_t>(rect.left) * source_bytes;
        output.pitch = static_cast<UINT>(image.pitch);
        return S_OK;
    }

    try {
        output.converted.resize(static_cast<std::size_t>(width) * height);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    auto* destination = output.converted.data();
    const bool standard_bgr = image.format.dwRBitMask == 0x00ff0000 &&
        image.format.dwGBitMask == 0x0000ff00 &&
        image.format.dwBBitMask == 0x000000ff &&
        (!image.format.dwRGBAlphaBitMask ||
         image.format.dwRGBAlphaBitMask == 0xff000000);
    if (standard_bgr && source_bytes == 3 &&
        !image.format.dwRGBAlphaBitMask) {
        dispatch_standard_bgr<3, false>(image, rect, width, height, destination);
        output.data = output.converted.data();
        output.pitch = width * 4u;
        return S_OK;
    }
    if (standard_bgr && source_bytes == 4) {
        if (image.format.dwRGBAlphaBitMask) {
            dispatch_standard_bgr<4, true>(image, rect, width, height, destination);
        } else {
            dispatch_standard_bgr<4, false>(image, rect, width, height, destination);
        }
        output.data = output.converted.data();
        output.pitch = width * 4u;
        return S_OK;
    }
    const ChannelDecoder red = make_channel_decoder(image.format.dwRBitMask, 0);
    const ChannelDecoder green = make_channel_decoder(image.format.dwGBitMask, 0);
    const ChannelDecoder blue = make_channel_decoder(image.format.dwBBitMask, 0);
    const ChannelDecoder alpha = make_channel_decoder(
        image.format.dwRGBAlphaBitMask, 255);
    switch (source_bytes) {
    case 1:
        dispatch_alpha_conversion<1>(image, rect, width, height,
                                     red, green, blue, alpha, destination);
        break;
    case 2:
        dispatch_alpha_conversion<2>(image, rect, width, height,
                                     red, green, blue, alpha, destination);
        break;
    case 3:
        dispatch_alpha_conversion<3>(image, rect, width, height,
                                     red, green, blue, alpha, destination);
        break;
    case 4:
        dispatch_alpha_conversion<4>(image, rect, width, height,
                                     red, green, blue, alpha, destination);
        break;
    default:
        if (image.format.dwRGBAlphaBitMask) {
            if (image.has_color_key) {
                convert_strided_pixels<true, true>(
                    image, rect, width, height, source_bytes,
                    red, green, blue, alpha, destination);
            } else {
                convert_strided_pixels<true, false>(
                    image, rect, width, height, source_bytes,
                    red, green, blue, alpha, destination);
            }
        } else if (image.has_color_key) {
            convert_strided_pixels<false, true>(
                image, rect, width, height, source_bytes,
                red, green, blue, alpha, destination);
        } else {
            convert_strided_pixels<false, false>(
                image, rect, width, height, source_bytes,
                red, green, blue, alpha, destination);
        }
        break;
    }
    output.data = output.converted.data();
    output.pitch = width * 4u;
    return S_OK;
}

} // namespace

#ifdef EID3D11_TESTING
void test_fail_next_surface_srv_creation() noexcept {
    fail_next_surface_srv_creation = true;
}
#endif

HRESULT create_completed_surface_texture(ID3D11Device* device, const SurfaceImage& image,
    ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv) {
    texture.Reset(); srv.Reset();
    if (!device || !image.width || !image.height || image.pitch <= 0 ||
        image.pixels.size() != std::size_t(image.pitch) * image.height) return E_INVALIDARG;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; UINT blocks = 0;
    if (!surface_dxgi_format(image.format,format,blocks) || blocks) return E_NOTIMPL;
    PreparedUpload upload{};
    HRESULT hr = prepare_upload(image,full_rect(image),0,upload);
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = image.width; description.Height = image.height;
    description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
    description.Format = format; description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{upload.data,upload.pitch,0};
    hr = device->CreateTexture2D(&description,&initial,&texture);
    if (FAILED(hr)) return hr;
    hr = device->CreateShaderResourceView(texture.Get(),nullptr,&srv);
    if (FAILED(hr)) texture.Reset();
    return hr;
}

HRESULT upload_surface_image(ID3D11Device* device, ID3D11DeviceContext* context,
                             SurfaceImage& image, SurfaceUploadResult* result) {
    if (result) *result = {};
    if (!device || !context || !image.width || !image.height || image.pitch <= 0)
        return E_INVALIDARG;

    DXGI_FORMAT target_format = DXGI_FORMAT_UNKNOWN;
    UINT block_bytes = 0;
    if (!surface_dxgi_format(image.format, target_format, block_bytes)) return E_NOTIMPL;
    const bool generate_mips=image.static_mip_hint&&image.world_mip_use&&!image.gpu_captured&&!block_bytes;
    const SurfaceUploadShape current_shape = surface_upload_shape(image);
    const bool shape_matches = same_surface_upload_shape(image.uploaded_shape, current_shape);
    const bool texture_matches = shape_matches &&
        matching_texture(image.texture.Get(), image.width, image.height, target_format,generate_mips);
    if (texture_matches && image.srv && image.uploaded_generation == image.generation)
        return S_OK;

    if (image.cpu_released) {
        // The texture is the only copy. Content changes must restore the CPU
        // bytes first (Renderer::restore_surface_pixels); never upload zeros.
        if (!shape_matches || image.uploaded_generation != image.generation || !image.texture)
            return E_UNEXPECTED;
        // Only the mip layout changed: rebuild from the current level 0 on the GPU.
        D3D11_TEXTURE2D_DESC description{};
        description.Width = image.width;
        description.Height = image.height;
        description.MipLevels = generate_mips?full_mip_count(image.width,image.height):1;
        description.ArraySize = 1;
        description.Format = target_format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if(generate_mips){description.BindFlags|=D3D11_BIND_RENDER_TARGET;description.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;}
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = device->CreateTexture2D(&description, nullptr, &texture);
        if (FAILED(hr)) return hr;
        hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (FAILED(hr)) return hr;
        context->CopySubresourceRegion(texture.Get(), 0, 0, 0, 0, image.texture.Get(), 0, nullptr);
        if (generate_mips) context->GenerateMips(srv.Get());
        image.mip_source_used = true;
        image.texture = texture;
        image.srv = srv;
        ++image.texture_serial;
        if (result) {
            result->uploaded = true;
            result->recreated = true;
            result->full_surface = true;
            result->dirty_rect = full_rect(image);
        }
        return S_OK;
    }

    const UINT source_rows = block_bytes ? std::max<UINT>(1, (image.height + 3) / 4) : image.height;
    const std::size_t required = static_cast<std::size_t>(image.pitch) * source_rows;
    try {
        if (image.pixels.size() < required) image.pixels.resize(required);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }

    const bool recreate = !texture_matches;
    const RECT dirty = normalize_dirty_rect(image, recreate, block_bytes);
    const bool full = is_full_rect(image, dirty);
    PreparedUpload upload{};
    HRESULT hr = prepare_upload(image, dirty, block_bytes, upload);
    if (FAILED(hr)) return hr;

    ComPtr<ID3D11Texture2D> texture = image.texture;
    ComPtr<ID3D11ShaderResourceView> srv = image.srv;
    if (recreate) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = image.width;
        description.Height = image.height;
        description.MipLevels = generate_mips?full_mip_count(image.width,image.height):1;
        description.ArraySize = 1;
        description.Format = target_format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if(generate_mips){description.BindFlags|=D3D11_BIND_RENDER_TARGET;description.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;}
        D3D11_SUBRESOURCE_DATA initial{upload.data, upload.pitch, 0};
        texture.Reset();
        srv.Reset();
        hr = device->CreateTexture2D(&description, generate_mips?nullptr:&initial, &texture);
        if (FAILED(hr)) return hr;
#ifdef EID3D11_TESTING
        if (fail_next_surface_srv_creation) {
            fail_next_surface_srv_creation = false;
            return E_OUTOFMEMORY;
        }
#endif
        hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (FAILED(hr)) return hr;
        if(generate_mips)context->UpdateSubresource(texture.Get(),0,nullptr,upload.data,upload.pitch,0);
    } else {
        if (!srv) {
            hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
            if (FAILED(hr)) return hr;
        }
        D3D11_BOX box{};
        const D3D11_BOX* box_pointer = nullptr;
        if (!full) {
            box.left = static_cast<UINT>(dirty.left);
            box.top = static_cast<UINT>(dirty.top);
            box.front = 0;
            box.right = static_cast<UINT>(dirty.right);
            box.bottom = static_cast<UINT>(dirty.bottom);
            box.back = 1;
            box_pointer = &box;
        }
        context->UpdateSubresource(texture.Get(), 0, box_pointer,
                                   upload.data, upload.pitch, 0);
    }

    if(generate_mips)context->GenerateMips(srv.Get());
    image.mip_source_used=true;
    if (recreate) ++image.texture_serial;
    image.texture = texture;
    image.srv = srv;
    image.uploaded_generation = image.generation;
    image.uploaded_shape = current_shape;
    image.gpu_captured = false;
    clear_surface_dirty(image);
    if (result) {
        result->uploaded = true;
        result->recreated = recreate;
        result->full_surface = full;
        result->dirty_rect = dirty;
        result->upload_bytes = upload.bytes;
    }
    return S_OK;
}

} // namespace owned
