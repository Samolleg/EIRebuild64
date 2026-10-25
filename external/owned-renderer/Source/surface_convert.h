#pragma once
// Game surface pixels to their GPU texture format. Header-only so the x86
// DLL (surface_upload.cpp, standard mode) and the x64 host (the remote
// surface protocol, Host/remote_host.cpp) run the same conversion.
// RGB surfaces become B8G8R8A8; DXT1-5 pass through as BC1-3. BC2/BC3 keep
// the encoded DXT2/DXT4 premultiplied values as supplied.
#include <windows.h>
#include <ddraw.h>
#include <dxgiformat.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace surface_convert {

// The pixels a conversion reads: `pixels` addresses texel (0, 0) of rows
// `pitch` bytes apart (block rows for DXT).
struct Source {
    const std::uint8_t* pixels = nullptr;
    std::size_t pitch = 0;
    DDPIXELFORMAT format{};
    bool has_color_key = false;
    DDCOLORKEY color_key{};
    // Bits the colour key compares: all for surface uploads; the colour bits
    // only for UI blits (their alpha comes from the key alone).
    std::uint32_t key_mask = 0xffffffffu;
};

inline bool target_format(const DDPIXELFORMAT& format, DXGI_FORMAT& output, UINT& block_bytes) noexcept {
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

namespace detail {

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

inline ChannelDecoder make_channel_decoder(std::uint32_t mask, std::uint8_t fallback) noexcept {
    ChannelDecoder decoder{};
    decoder.mask = mask;
    decoder.fallback = fallback;
    if (!mask) return decoder;
    while (((mask >> decoder.shift) & 1u) == 0u) ++decoder.shift;
    decoder.maximum = mask >> decoder.shift;
    decoder.use_lookup = decoder.maximum <= 255u;
    if (decoder.use_lookup) {
        for (std::uint32_t value = 0; value <= decoder.maximum; ++value)
            decoder.lookup[value] = static_cast<std::uint8_t>(value * 255u / decoder.maximum);
    }
    return decoder;
}

template <UINT SourceBytes> std::uint32_t load_pixel(const std::uint8_t* source) noexcept {
    if constexpr (SourceBytes == 1) {
        return source[0];
    } else if constexpr (SourceBytes == 2) {
        return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8);
    } else if constexpr (SourceBytes == 3) {
        return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8) |
            (static_cast<std::uint32_t>(source[2]) << 16);
    } else {
        std::uint32_t raw = 0;
        std::memcpy(&raw, source, sizeof(raw));
        return raw;
    }
}

inline bool key_match(const Source& image, std::uint32_t raw) noexcept {
    const std::uint32_t value = raw & image.key_mask;
    return value >= image.color_key.dwColorSpaceLowValue && value <= image.color_key.dwColorSpaceHighValue;
}

inline const std::uint8_t* row_at(const Source& image, const RECT& rect, UINT y, UINT source_bytes) noexcept {
    return image.pixels + static_cast<std::size_t>(rect.top + static_cast<LONG>(y)) * image.pitch +
        static_cast<std::size_t>(rect.left) * source_bytes;
}

template <UINT SourceBytes, bool HasAlpha, bool HasColorKey>
void convert_pixels(const Source& image, const RECT& rect, UINT width, UINT height, const ChannelDecoder& red,
                    const ChannelDecoder& green, const ChannelDecoder& blue, const ChannelDecoder& alpha,
                    std::uint32_t* destination) noexcept {
    for (UINT y = 0; y < height; ++y) {
        const auto* source = row_at(image, rect, y, SourceBytes);
        auto* row = destination + static_cast<std::size_t>(y) * width;
        for (UINT x = 0; x < width; ++x, source += SourceBytes) {
            const std::uint32_t raw = load_pixel<SourceBytes>(source);
            std::uint8_t a = HasAlpha ? alpha.decode(raw) : 255u;
            if constexpr (HasColorKey) {
                if (key_match(image, raw)) a = 0;
            }
            row[x] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(red.decode(raw)) << 16) |
                (static_cast<std::uint32_t>(green.decode(raw)) << 8) | blue.decode(raw);
        }
    }
}

template <bool HasAlpha, bool HasColorKey>
void convert_strided_pixels(const Source& image, const RECT& rect, UINT width, UINT height, UINT source_bytes,
                            const ChannelDecoder& red, const ChannelDecoder& green, const ChannelDecoder& blue,
                            const ChannelDecoder& alpha, std::uint32_t* destination) noexcept {
    for (UINT y = 0; y < height; ++y) {
        const auto* source = row_at(image, rect, y, source_bytes);
        auto* row = destination + static_cast<std::size_t>(y) * width;
        for (UINT x = 0; x < width; ++x, source += source_bytes) {
            const std::uint32_t raw = load_pixel<4>(source);
            std::uint8_t a = HasAlpha ? alpha.decode(raw) : 255u;
            if constexpr (HasColorKey) {
                if (key_match(image, raw)) a = 0;
            }
            row[x] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(red.decode(raw)) << 16) |
                (static_cast<std::uint32_t>(green.decode(raw)) << 8) | blue.decode(raw);
        }
    }
}

template <UINT SourceBytes, bool HasAlpha, bool HasColorKey>
void convert_standard_bgr(const Source& image, const RECT& rect, UINT width, UINT height,
                          std::uint32_t* destination) noexcept {
    for (UINT y = 0; y < height; ++y) {
        const auto* source = row_at(image, rect, y, SourceBytes);
        auto* row = destination + static_cast<std::size_t>(y) * width;
        for (UINT x = 0; x < width; ++x, source += SourceBytes) {
            const std::uint32_t raw = load_pixel<SourceBytes>(source);
            std::uint32_t alpha = HasAlpha ? (raw & 0xff000000u) : 0xff000000u;
            if constexpr (HasColorKey) {
                if (key_match(image, raw)) alpha = 0;
            }
            row[x] = alpha | (raw & 0x00ffffffu);
        }
    }
}

template <UINT SourceBytes, bool HasAlpha>
void dispatch_standard_bgr(const Source& image, const RECT& rect, UINT width, UINT height,
                           std::uint32_t* destination) noexcept {
    if (image.has_color_key) convert_standard_bgr<SourceBytes, HasAlpha, true>(image, rect, width, height, destination);
    else convert_standard_bgr<SourceBytes, HasAlpha, false>(image, rect, width, height, destination);
}

template <UINT SourceBytes, bool HasAlpha>
void dispatch_keyed_conversion(const Source& image, const RECT& rect, UINT width, UINT height,
                               const ChannelDecoder& red, const ChannelDecoder& green, const ChannelDecoder& blue,
                               const ChannelDecoder& alpha, std::uint32_t* destination) noexcept {
    if (image.has_color_key)
        convert_pixels<SourceBytes, HasAlpha, true>(image, rect, width, height, red, green, blue, alpha, destination);
    else
        convert_pixels<SourceBytes, HasAlpha, false>(image, rect, width, height, red, green, blue, alpha, destination);
}

template <UINT SourceBytes>
void dispatch_alpha_conversion(const Source& image, const RECT& rect, UINT width, UINT height,
                               const ChannelDecoder& red, const ChannelDecoder& green, const ChannelDecoder& blue,
                               const ChannelDecoder& alpha, std::uint32_t* destination) noexcept {
    if (image.format.dwRGBAlphaBitMask)
        dispatch_keyed_conversion<SourceBytes, true>(image, rect, width, height, red, green, blue, alpha, destination);
    else
        dispatch_keyed_conversion<SourceBytes, false>(image, rect, width, height, red, green, blue, alpha, destination);
}

} // namespace detail

// The bytes UpdateSubresource takes for `rect` (block-aligned for DXT):
// `data` points into the source when no conversion is needed, else into
// `converted` (tight B8G8R8A8 rows).
struct Prepared {
    const void* data = nullptr;
    UINT pitch = 0;
    std::uint64_t bytes = 0;
    std::vector<std::uint32_t> converted;
};

inline HRESULT prepare(const Source& image, const RECT& rect, UINT block_bytes, Prepared& output) {
    using namespace detail;
    const UINT width = static_cast<UINT>(rect.right - rect.left);
    const UINT height = static_cast<UINT>(rect.bottom - rect.top);
    if (block_bytes) {
        output.data = image.pixels + static_cast<std::size_t>(rect.top / 4) * image.pitch +
            static_cast<std::size_t>(rect.left / 4) * block_bytes;
        output.pitch = static_cast<UINT>(image.pitch);
        output.bytes = static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4) * block_bytes;
        return S_OK;
    }

    const UINT source_bytes = (std::max)(1u, static_cast<UINT>(image.format.dwRGBBitCount / 8));
    const bool direct_bgra = source_bytes == 4 && !image.has_color_key && image.format.dwRBitMask == 0x00ff0000 &&
        image.format.dwGBitMask == 0x0000ff00 && image.format.dwBBitMask == 0x000000ff &&
        image.format.dwRGBAlphaBitMask == 0xff000000;
    output.bytes = static_cast<std::uint64_t>(width) * height * 4u;
    if (direct_bgra) {
        output.data = row_at(image, rect, 0, source_bytes);
        output.pitch = static_cast<UINT>(image.pitch);
        return S_OK;
    }

    try {
        output.converted.resize(static_cast<std::size_t>(width) * height);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    auto* destination = output.converted.data();
    output.data = destination;
    output.pitch = width * 4u;
    const bool standard_bgr = image.format.dwRBitMask == 0x00ff0000 && image.format.dwGBitMask == 0x0000ff00 &&
        image.format.dwBBitMask == 0x000000ff &&
        (!image.format.dwRGBAlphaBitMask || image.format.dwRGBAlphaBitMask == 0xff000000);
    if (standard_bgr && source_bytes == 3 && !image.format.dwRGBAlphaBitMask) {
        dispatch_standard_bgr<3, false>(image, rect, width, height, destination);
        return S_OK;
    }
    if (standard_bgr && source_bytes == 4) {
        if (image.format.dwRGBAlphaBitMask) dispatch_standard_bgr<4, true>(image, rect, width, height, destination);
        else dispatch_standard_bgr<4, false>(image, rect, width, height, destination);
        return S_OK;
    }
    const ChannelDecoder red = make_channel_decoder(image.format.dwRBitMask, 0);
    const ChannelDecoder green = make_channel_decoder(image.format.dwGBitMask, 0);
    const ChannelDecoder blue = make_channel_decoder(image.format.dwBBitMask, 0);
    const ChannelDecoder alpha = make_channel_decoder(image.format.dwRGBAlphaBitMask, 255);
    switch (source_bytes) {
    case 1: dispatch_alpha_conversion<1>(image, rect, width, height, red, green, blue, alpha, destination); break;
    case 2: dispatch_alpha_conversion<2>(image, rect, width, height, red, green, blue, alpha, destination); break;
    case 3: dispatch_alpha_conversion<3>(image, rect, width, height, red, green, blue, alpha, destination); break;
    case 4: dispatch_alpha_conversion<4>(image, rect, width, height, red, green, blue, alpha, destination); break;
    default:
        if (image.format.dwRGBAlphaBitMask) {
            if (image.has_color_key)
                convert_strided_pixels<true, true>(image, rect, width, height, source_bytes, red, green, blue, alpha, destination);
            else
                convert_strided_pixels<true, false>(image, rect, width, height, source_bytes, red, green, blue, alpha, destination);
        } else if (image.has_color_key) {
            convert_strided_pixels<false, true>(image, rect, width, height, source_bytes, red, green, blue, alpha, destination);
        } else {
            convert_strided_pixels<false, false>(image, rect, width, height, source_bytes, red, green, blue, alpha, destination);
        }
        break;
    }
    return S_OK;
}

} // namespace surface_convert
