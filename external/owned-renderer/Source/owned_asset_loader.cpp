#include "owned_asset_loader.h"
#include "debug_output_paths.h"
#include "owned_terrain_front_end.h"
#include "owned_terrain_decode.h"
#include "procedural_falls.h"
#include "worker_priority.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace owned {
#ifdef EID3D11_TESTING
static std::atomic<std::uint64_t> g_texture_alpha_inspections{0};
std::uint64_t texture_alpha_inspections() noexcept {
    return g_texture_alpha_inspections.load(std::memory_order_relaxed);
}
#endif

bool texture_alpha_fully_opaque(const TextureAlphaPixels& source) noexcept {
#ifdef EID3D11_TESTING
    g_texture_alpha_inspections.fetch_add(1, std::memory_order_relaxed);
#endif
    const bool packed = source.format == TextureAlphaFormat::packed;
    const std::uint32_t block_bytes = source.format == TextureAlphaFormat::bc1 ? 8u : 16u;
    if (!source.data || !source.width || !source.height ||
        (packed && (source.pixel_bytes < 2 || source.pixel_bytes > 4)) ||
        (!packed && source.format != TextureAlphaFormat::bc1 &&
         source.format != TextureAlphaFormat::bc2 && source.format != TextureAlphaFormat::bc3))
        return false;
    const std::uint64_t columns = packed ? source.width : (std::uint64_t(source.width) + 3) / 4;
    const std::uint64_t rows = packed ? source.height : (std::uint64_t(source.height) + 3) / 4;
    const std::uint64_t row_bytes = columns * (packed ? source.pixel_bytes : block_bytes);
    if (row_bytes > source.pitch || (rows - 1) * source.pitch + row_bytes > source.bytes)
        return false;
    if (packed) {
        if (source.alpha_mask) {
            unsigned shift = 0;
            while (!(source.alpha_mask & (std::uint32_t{1} << shift))) ++shift;
            // Match the uploader's uint32 UNORM expansion, including masks
            // whose multiplication cannot produce an alpha255 result.
            if ((source.alpha_mask >> shift) > UINT32_MAX / 255u) return false;
        }
        if (!source.alpha_mask && !source.has_color_key) return true;
        for (std::uint32_t y = 0; y < source.height; ++y)
            for (std::uint32_t x = 0; x < source.width; ++x) {
                std::uint32_t raw = 0;
                std::memcpy(&raw, source.data + std::size_t(y) * source.pitch +
                    std::size_t(x) * source.pixel_bytes, source.pixel_bytes);
                if ((raw & source.alpha_mask) != source.alpha_mask ||
                    (source.has_color_key && raw >= source.key_low && raw <= source.key_high))
                    return false;
            }
        return true;
    }
    for (std::uint64_t y = 0; y < rows; ++y)
        for (std::uint64_t x = 0; x < columns; ++x) {
            const auto* block = source.data + std::size_t(y * source.pitch + x * block_bytes);
            std::uint64_t alpha = 0;
            if (source.format == TextureAlphaFormat::bc1) {
                std::uint16_t first = 0, second = 0;
                std::memcpy(&first, block, 2); std::memcpy(&second, block + 2, 2);
                if (first > second) continue;
                std::memcpy(&alpha, block + 4, 4);
            } else if (source.format == TextureAlphaFormat::bc2) {
                std::memcpy(&alpha, block, 8);
            } else {
                std::memcpy(&alpha, block + 2, 6);
            }
            // Ignore block padding outside the actual mip dimensions.
            for (unsigned dy = 0; dy < 4 && y * 4 + dy < source.height; ++dy)
                for (unsigned dx = 0; dx < 4 && x * 4 + dx < source.width; ++dx) {
                    const unsigned texel = dy * 4 + dx;
                    if (source.format == TextureAlphaFormat::bc1) {
                        if (((alpha >> (texel * 2)) & 3) == 3) return false;
                    } else if (source.format == TextureAlphaFormat::bc2) {
                        if (((alpha >> (texel * 4)) & 15) != 15) return false;
                    } else {
                        const unsigned selector = unsigned((alpha >> (texel * 3)) & 7);
                        const bool opaque = selector < 2 ? block[selector] == 255 :
                            (block[0] <= block[1] && (selector == 7 ||
                             (selector < 6 && block[0] == 255 && block[1] == 255)));
                        if (!opaque) return false;
                    }
                }
        }
    return true;
}

bool texture_alpha_texel_cut(const TextureAlphaPixels& source,
                             std::uint32_t x, std::uint32_t y) noexcept {
    if (!source.data || x >= source.width || y >= source.height) return false;
    if (source.format == TextureAlphaFormat::cut_bits) {
        const std::size_t bit = std::size_t(y) * source.width + x;
        return bit / 8 < source.bytes && ((source.data[bit / 8] >> (bit % 8)) & 1u);
    }
    if (source.format == TextureAlphaFormat::packed) {
        if (source.pixel_bytes < 2 || source.pixel_bytes > 4) return false;
        const std::size_t offset = std::size_t(y) * source.pitch + std::size_t(x) * source.pixel_bytes;
        if (offset + source.pixel_bytes > source.bytes) return false;
        std::uint32_t raw = 0;
        std::memcpy(&raw, source.data + offset, source.pixel_bytes);
        return (raw & source.alpha_mask) != source.alpha_mask ||
            (source.has_color_key && raw >= source.key_low && raw <= source.key_high);
    }
    const std::uint32_t block_bytes = source.format == TextureAlphaFormat::bc1 ? 8u : 16u;
    const std::size_t offset = std::size_t(y / 4) * source.pitch + std::size_t(x / 4) * block_bytes;
    if (offset + block_bytes > source.bytes) return false;
    const auto* block = source.data + offset;
    const unsigned texel = (y % 4) * 4 + x % 4;
    std::uint64_t alpha = 0;
    switch (source.format) {
    case TextureAlphaFormat::bc1: {
        std::uint16_t first = 0, second = 0;
        std::memcpy(&first, block, 2); std::memcpy(&second, block + 2, 2);
        if (first > second) return false;
        std::memcpy(&alpha, block + 4, 4);
        return ((alpha >> (texel * 2)) & 3) == 3;
    }
    case TextureAlphaFormat::bc2:
        std::memcpy(&alpha, block, 8);
        return ((alpha >> (texel * 4)) & 15) != 15;
    case TextureAlphaFormat::bc3: {
        std::memcpy(&alpha, block + 2, 6);
        const unsigned selector = unsigned((alpha >> (texel * 3)) & 7);
        const bool opaque = selector < 2 ? block[selector] == 255 :
            (block[0] <= block[1] && (selector == 7 ||
             (selector < 6 && block[0] == 255 && block[1] == 255)));
        return !opaque;
    }
    default: return false;
    }
}

bool texture_alpha_texel_empty(const TextureAlphaPixels& source,
                               std::uint32_t x, std::uint32_t y) noexcept {
    if (!source.data || x >= source.width || y >= source.height ||
        source.format == TextureAlphaFormat::cut_bits) return false;
    if (source.format == TextureAlphaFormat::bc1)
        return texture_alpha_texel_cut(source, x, y);
    if (source.format == TextureAlphaFormat::packed) {
        if (source.pixel_bytes < 2 || source.pixel_bytes > 4) return false;
        const std::size_t offset = std::size_t(y) * source.pitch + std::size_t(x) * source.pixel_bytes;
        if (offset + source.pixel_bytes > source.bytes) return false;
        std::uint32_t raw = 0;
        std::memcpy(&raw, source.data + offset, source.pixel_bytes);
        return (source.alpha_mask && !(raw & source.alpha_mask)) ||
            (source.has_color_key && raw >= source.key_low && raw <= source.key_high);
    }
    const std::size_t offset = std::size_t(y / 4) * source.pitch + std::size_t(x / 4) * 16u;
    if (offset + 16u > source.bytes) return false;
    const auto* block = source.data + offset;
    const unsigned texel = (y % 4) * 4 + x % 4;
    std::uint64_t alpha = 0;
    if (source.format == TextureAlphaFormat::bc2) {
        std::memcpy(&alpha, block, 8);
        return ((alpha >> (texel * 4)) & 15) == 0;
    }
    if (source.format != TextureAlphaFormat::bc3) return false;
    std::memcpy(&alpha, block + 2, 6);
    const unsigned selector = unsigned((alpha >> (texel * 3)) & 7);
    if (selector < 2) return block[selector] == 0;
    if (block[0] > block[1])
        return ((8 - selector) * unsigned(block[0]) + (selector - 1) * unsigned(block[1])) / 7 == 0;
    if (selector >= 6) return selector == 6;
    return ((6 - selector) * unsigned(block[0]) + (selector - 1) * unsigned(block[1])) / 5 == 0;
}

std::shared_ptr<const std::vector<std::uint8_t>> texture_alpha_cut_mask(
    const TextureAlphaPixels& source) {
    if (!source.width || !source.height) return nullptr;
    auto mask = std::make_shared<std::vector<std::uint8_t>>(
        (std::size_t(source.width) * source.height + 7) / 8, std::uint8_t{0});
    // Packed surfaces (composed unit atlases are 1024x1024x32) apply the
    // texel rule in one pass instead of a bounds-checked call per texel.
    // Identical result; a short or malformed buffer takes the general loop.
    if (source.format == TextureAlphaFormat::packed && source.data &&
        source.pixel_bytes >= 2 && source.pixel_bytes <= 4 &&
        std::uint64_t(source.width) * source.pixel_bytes <= source.pitch &&
        std::uint64_t(source.height - 1) * source.pitch +
            std::uint64_t(source.width) * source.pixel_bytes <= source.bytes) {
        const auto run = [&](auto read) {
            auto* bits = mask->data();
            std::size_t bit = 0;
            for (std::uint32_t y = 0; y < source.height; ++y) {
                const auto* row = source.data + std::size_t(y) * source.pitch;
                for (std::uint32_t x = 0; x < source.width; ++x, ++bit) {
                    const std::uint32_t raw = read(row + std::size_t(x) * source.pixel_bytes);
                    if ((raw & source.alpha_mask) != source.alpha_mask ||
                        (source.has_color_key && raw >= source.key_low && raw <= source.key_high))
                        bits[bit / 8] |= std::uint8_t(1u << (bit % 8));
                }
            }
        };
        if (source.pixel_bytes == 4)
            run([](const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; });
        else if (source.pixel_bytes == 2)
            run([](const std::uint8_t* p) { std::uint16_t v; std::memcpy(&v, p, 2); return std::uint32_t(v); });
        else
            run([](const std::uint8_t* p) {
                return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16); });
        return mask;
    }
    for (std::uint32_t y = 0; y < source.height; ++y)
        for (std::uint32_t x = 0; x < source.width; ++x)
            if (texture_alpha_texel_cut(source, x, y)) {
                const std::size_t bit = std::size_t(y) * source.width + x;
                (*mask)[bit / 8] |= std::uint8_t(1u << (bit % 8));
            }
    return mask;
}

bool visual_surface_alpha_pixels(const OwnedVisualSurface& surface,
                                 TextureAlphaPixels& alpha) noexcept {
    alpha = {};
    if (!surface.width || !surface.height ||
        (!surface.alpha_fully_opaque && !surface.alpha_cut_mask)) return false;
    alpha.width = surface.width; alpha.height = surface.height;
    alpha.format = TextureAlphaFormat::cut_bits;
    if (surface.alpha_cut_mask) {
        alpha.data = surface.alpha_cut_mask->data();
        alpha.bytes = surface.alpha_cut_mask->size();
    }
    return true;
}

bool visual_surface_raw_alpha(const OwnedVisualSurface& surface,
                              const std::uint8_t* data, std::size_t bytes,
                              TextureAlphaPixels& alpha) noexcept {
    alpha = {};
    if (!data || !bytes || surface.pitch <= 0) return false;
    alpha.data = data; alpha.bytes = bytes;
    alpha.width = surface.width; alpha.height = surface.height;
    alpha.pitch = static_cast<std::uint32_t>(surface.pitch);
    if (surface.format.dwFlags & DDPF_FOURCC) {
        switch (surface.format.dwFourCC) {
        case MAKEFOURCC('D','X','T','1'): alpha.format = TextureAlphaFormat::bc1; break;
        case MAKEFOURCC('D','X','T','2'): case MAKEFOURCC('D','X','T','3'):
            alpha.format = TextureAlphaFormat::bc2; break;
        case MAKEFOURCC('D','X','T','4'): case MAKEFOURCC('D','X','T','5'):
            alpha.format = TextureAlphaFormat::bc3; break;
        default: alpha.format = static_cast<TextureAlphaFormat>(255); break;
        }
    } else {
        alpha.pixel_bytes = surface.format.dwRGBBitCount / 8;
        alpha.alpha_mask = surface.format.dwRGBAlphaBitMask;
        alpha.has_color_key = surface.has_color_key;
        alpha.key_low = surface.color_key.dwColorSpaceLowValue;
        alpha.key_high = surface.color_key.dwColorSpaceHighValue;
    }
    return true;
}

namespace {

constexpr std::uint32_t kMobMain = 0x0000a000u;
constexpr std::uint32_t kMobQuest = 0x0000d000u;
constexpr std::uint32_t kMobZonal = 0x0000c000u;
constexpr std::uint32_t kMobObjects = 0x0000b000u;
constexpr std::uint32_t kMobObject = 0x0000b001u;
constexpr std::uint32_t kMobUnit = 0xbbbb0000u;
constexpr std::uint32_t kMobUnitLogic = 0xbbbc0000u;
constexpr std::uint32_t kMobLever = 0xbbac0000u;
constexpr std::uint32_t kMobTrap = 0xbbab0000u;

constexpr std::uint32_t kFieldParts = 0x0000b00du;
constexpr std::uint32_t kFieldOwner = 0x0000b011u;
constexpr std::uint32_t kFieldNid = 0x0000b002u;
constexpr std::uint32_t kFieldType = 0x0000b003u;
constexpr std::uint32_t kFieldName = 0x0000b004u;
constexpr std::uint32_t kFieldModel = 0x0000b006u;
constexpr std::uint32_t kFieldParentName = 0x0000b00eu;
constexpr std::uint32_t kFieldTexture0 = 0x0000b007u;
constexpr std::uint32_t kFieldTexture1 = 0x0000b008u;
constexpr std::uint32_t kFieldComment = 0x0000b00fu;
constexpr std::uint32_t kFieldPosition = 0x0000b009u;
constexpr std::uint32_t kFieldQuaternion = 0x0000b00au;
constexpr std::uint32_t kFieldQuest = 0x0000b013u;
constexpr std::uint32_t kFieldShadow = 0x0000b014u;
constexpr std::uint32_t kFieldParentNid = 0x0000b012u;
constexpr std::uint32_t kFieldQuestInfo = 0x0000b016u;
constexpr std::uint32_t kFieldShape = 0x0000b00cu;

constexpr std::uint32_t kFig1 = 0x31474946u;
constexpr std::uint32_t kMmp = 0x00504d4du;
constexpr std::uint32_t kDxt1 = 0x31545844u;
constexpr std::uint32_t kDxt3 = 0x33545844u;
constexpr std::uint32_t kDxt5 = 0x35545844u;
constexpr std::uint32_t kPnt3 = 0x33544e50u;
constexpr std::uint32_t kRgb565 = 0x00005650u;
constexpr std::uint32_t kArgb1555 = 0x00005551u;
constexpr std::uint32_t kArgb4444 = 0x00004444u;
constexpr std::uint32_t kArgb8888 = 0x00008888u;
constexpr std::size_t kMmpHeaderBytes = 76;
constexpr std::uint32_t kMapSignature = 0xce4af672u;
constexpr std::uint32_t kSectorSignatureNormals = 0xcf4bf774u;
constexpr std::size_t kMapHeaderBytes = 38u;
constexpr std::size_t kMapObjectBytes = 44u;
constexpr std::uint32_t kTerrainCellsPerSector = 32u;
constexpr std::uint32_t kTerrainVerticesPerSide = 33u;
constexpr std::uint32_t kTerrainVerticesPerSector = 33u * 33u;
constexpr std::size_t kTerrainVertexBytes = 8u;
constexpr std::size_t kTerrainTileBytes = 16u * 16u * sizeof(std::uint16_t);
constexpr std::uint32_t kTerrainTilesPerSide = 16u;
constexpr std::uint32_t kTerrainTilesPerSector = 16u * 16u;
constexpr std::uint32_t kTerrainLongTextureCount = 2u;
constexpr std::uint32_t kTerrainTilesPerLongTexture = 8u;
constexpr std::uint16_t kTerrainUndefined = UINT16_MAX;

void decode_terrain_normal(std::uint32_t packed, TerrainVertex& vertex) noexcept {
    vertex.normal_z = static_cast<float>(packed >> 22u) / 1000.0f;
    vertex.normal_x =
        (static_cast<float>((packed >> 11u) & 0x7ffu) - 1000.0f) / 1000.0f;
    vertex.normal_y =
        (static_cast<float>(packed & 0x7ffu) - 1000.0f) / 1000.0f;
}

void add_terrain_adjacent_tile(std::array<std::uint16_t, 4>& positions,
                               std::uint32_t& count, int x, int y) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<int>(kTerrainTilesPerSide) ||
        y >= static_cast<int>(kTerrainTilesPerSide))
        return;
    positions[count++] = static_cast<std::uint16_t>(
        y * static_cast<int>(kTerrainTilesPerSide) + x);
}

std::array<std::uint16_t, 4> terrain_adjacent_tiles(
    std::uint32_t x, std::uint32_t y, std::uint32_t& count) noexcept {
    std::array<std::uint16_t, 4> positions{};
    count = 0;
    const int tile_x = static_cast<int>(x / 2u);
    const int tile_y = static_cast<int>(y / 2u);
    if ((x & 1u) == 0 && (y & 1u) == 0) {
        add_terrain_adjacent_tile(positions, count, tile_x - 1, tile_y - 1);
        add_terrain_adjacent_tile(positions, count, tile_x - 1, tile_y);
        add_terrain_adjacent_tile(positions, count, tile_x, tile_y - 1);
        add_terrain_adjacent_tile(positions, count, tile_x, tile_y);
    } else if ((x & 1u) == 0) {
        add_terrain_adjacent_tile(positions, count, tile_x - 1, tile_y);
        add_terrain_adjacent_tile(positions, count, tile_x, tile_y);
    } else if ((y & 1u) == 0) {
        add_terrain_adjacent_tile(positions, count, tile_x, tile_y - 1);
        add_terrain_adjacent_tile(positions, count, tile_x, tile_y);
    } else {
        add_terrain_adjacent_tile(positions, count, tile_x, tile_y);
    }
    return positions;
}

bool terrain_material(std::uint32_t tile, const TerrainCpuAsset& terrain,
                      std::uint16_t& material, std::uint32_t& position) noexcept {
    if (!terrain.tiles_per_texture || !terrain.texture_count) return false;
    const std::uint32_t ordinary = tile / terrain.tiles_per_texture;
    std::uint32_t value = ordinary;
    if (ordinary >= terrain.texture_count) {
        const std::uint64_t first_long =
            static_cast<std::uint64_t>(terrain.texture_count) *
            terrain.tiles_per_texture;
        if (tile < first_long) return false;
        const std::uint32_t long_tile = static_cast<std::uint32_t>(tile - first_long);
        value = terrain.texture_count + long_tile / kTerrainTilesPerLongTexture;
        position = long_tile % kTerrainTilesPerLongTexture;
    } else {
        position = tile % terrain.tiles_per_texture;
    }
    if (value >= terrain.texture_count + kTerrainLongTextureCount ||
        value > UINT16_MAX)
        return false;
    material = static_cast<std::uint16_t>(value);
    return true;
}

std::array<NativeTextureCoordinate, 9> terrain_uvs(
    std::uint32_t rotation, std::uint32_t position,
    const TerrainCpuAsset& terrain, bool long_tile) noexcept {
    std::array<NativeTextureCoordinate, 9> result{};
    const float tile = 1.0f / static_cast<float>(terrain.tiles_per_texture_side);
    const float margin = tile / static_cast<float>(terrain.tile_size) * 8.0f;
    const float height = long_tile ? 1.0f : tile;
    for (std::uint32_t y = 0; y < 3u; ++y)
        for (std::uint32_t x = 0; x < 3u; ++x) {
            const std::uint32_t index = y * 3u + x;
            result[index].u = x == 0u ? margin : (x == 1u ? tile * 0.5f : tile - margin);
            const float source_v = y == 0u ? margin :
                (y == 1u ? height * 0.5f : height - margin);
            result[index].v = source_v;
        }
    for (std::uint32_t turn = 0; turn < rotation; ++turn) {
        const auto corner = result[0];
        result[0] = result[2];
        result[2] = result[8];
        result[8] = result[6];
        result[6] = corner;
        const auto middle = result[1];
        result[1] = result[5];
        result[5] = result[7];
        result[7] = result[3];
        result[3] = middle;
    }
    const std::uint32_t x = long_tile ? position :
        position % terrain.tiles_per_texture_side;
    const std::uint32_t y = long_tile ? 0u :
        position / terrain.tiles_per_texture_side;
    for (auto& uv : result) {
        uv.u += static_cast<float>(x) * tile;
        uv.v = 1.0f - (uv.v + static_cast<float>(y) * tile);
    }
    return result;
}

void fail(ResourceFailure& failure, ResourceFailureCode code,
          const std::string& message) {
    failure.code = code;
    failure.message = message;
}

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t& output) noexcept {
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) return false;
    output = left + right;
    return true;
}

bool checked_multiply(std::uint64_t left, std::uint64_t right,
                      std::uint64_t& output) noexcept {
    if (left && right > (std::numeric_limits<std::uint64_t>::max)() / left)
        return false;
    output = left * right;
    return true;
}

bool decode_pnt3_fresh_zero(const std::uint8_t* encoded,
                            std::size_t encoded_bytes,
                            std::uint32_t width, std::uint32_t height,
                            std::vector<std::uint8_t>& decoded) noexcept {
    std::uint64_t pixel_count64 = 0;
    std::uint64_t output_bytes64 = 0;
    if (!encoded || !width || !height ||
        !checked_multiply(width, height, pixel_count64) ||
        (pixel_count64 & 3u) != 0u ||
        !checked_multiply(pixel_count64, 4u, output_bytes64) ||
        pixel_count64 > (std::numeric_limits<std::uint32_t>::max)() ||
        output_bytes64 > (std::numeric_limits<std::size_t>::max)())
        return false;
    const auto output_bytes = static_cast<std::size_t>(output_bytes64);
    try {
        decoded.assign(output_bytes, 0u);
    } catch (...) {
        return false;
    }

    std::size_t source = 0;
    std::size_t destination = 0;
    while (destination < output_bytes) {
        if (source > encoded_bytes || encoded_bytes - source < 4u)
            return false;
        std::uint32_t first = 0;
        std::memcpy(&first, encoded + source, sizeof(first));
        if ((first >> 24u) != 0u || first == 0u) {
            if (encoded_bytes - source < 16u ||
                output_bytes - destination < 16u)
                return false;
            std::memcpy(decoded.data() + destination, encoded + source, 16u);
            source += 16u;
            destination += 16u;
            continue;
        }

        const std::uint32_t skip_bytes = first;
        if (skip_bytes < 16u || (skip_bytes & 15u) != 0u ||
            skip_bytes > output_bytes - destination)
            return false;
        source += 4u;
        destination += skip_bytes;
    }
    return source == encoded_bytes;
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1]) << 8;
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
        static_cast<std::uint32_t>(bytes[1]) << 8 |
        static_cast<std::uint32_t>(bytes[2]) << 16 |
        static_cast<std::uint32_t>(bytes[3]) << 24;
}

float read_float(const std::uint8_t* bytes) noexcept {
    const std::uint32_t bits = read_u32(bytes);
    float output = 0.0f;
    std::memcpy(&output, &bits, sizeof(output));
    return output;
}

std::string fold_ascii(std::string value) {
    for (char& byte : value)
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte + 32);
    return value;
}

bool finite(const std::array<float, 3>& value) noexcept {
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool finite(const std::array<float, 4>& value) noexcept {
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]) && std::isfinite(value[3]);
}

bool mob_container(std::uint32_t id) noexcept {
    switch (id) {
    case kMobMain:
    case kMobQuest:
    case kMobZonal:
    case kMobObjects:
    case kMobObject:
    case kMobUnit:
    case kMobUnitLogic:
    case kMobLever:
    case kMobTrap:
        return true;
    default:
        return false;
    }
}

bool parse_mob_chunks(const std::vector<std::uint8_t>& bytes,
                      std::uint32_t begin, std::uint32_t end,
                      std::uint32_t depth,
                      std::vector<MobChunkRecord>& output,
                      ResourceFailure& failure) {
    if (depth > 32) {
        fail(failure, ResourceFailureCode::parse,
             "MOB nesting exceeds 32 levels");
        return false;
    }
    std::uint32_t cursor = begin;
    while (cursor < end) {
        // The game reads the file's root chunk and stops when it is consumed;
        // top-level bytes after it, whole or partial, are never read.
        if (depth == 0 && !output.empty() &&
            (end - cursor < 8 || read_u32(bytes.data() + cursor + 4) < 8 ||
             read_u32(bytes.data() + cursor + 4) > end - cursor)) {
            char detail[96];
            std::snprintf(detail, sizeof(detail), "%u trailing MOB bytes ignored, as the game does",
                          end - cursor);
            bridge_append_probe_event("mob_trailing_bytes", detail);
            return true;
        }
        if (end - cursor < 8) {
            fail(failure, ResourceFailureCode::parse,
                 "MOB has trailing bytes shorter than a chunk header");
            return false;
        }
        const std::uint32_t id = read_u32(bytes.data() + cursor);
        const std::uint32_t total = read_u32(bytes.data() + cursor + 4);
        if (total < 8 || total > end - cursor) {
            fail(failure, ResourceFailureCode::parse,
                 "MOB chunk length is outside its parent");
            return false;
        }
        MobChunkRecord record{};
        record.id = id;
        record.total_bytes = total;
        record.payload_offset = cursor + 8;
        record.payload_bytes = total - 8;
        if (mob_container(id) &&
            !parse_mob_chunks(bytes, record.payload_offset, cursor + total,
                              depth + 1, record.children, failure))
            return false;
        output.push_back(std::move(record));
        cursor += total;
    }
    return cursor == end;
}

bool known_object_field(std::uint32_t id) noexcept {
    switch (id) {
    case kFieldParts:
    case kFieldOwner:
    case kFieldNid:
    case kFieldType:
    case kFieldName:
    case kFieldModel:
    case kFieldParentName:
    case kFieldTexture0:
    case kFieldTexture1:
    case kFieldComment:
    case kFieldPosition:
    case kFieldQuaternion:
    case kFieldQuest:
    case kFieldShadow:
    case kFieldParentNid:
    case kFieldQuestInfo:
    case kFieldShape:
        return true;
    default:
        return false;
    }
}

bool legacy_string(const std::vector<std::uint8_t>& bytes,
                   const MobChunkRecord* record, std::string& output) {
    output.clear();
    if (!record) return true;
    const char* begin = reinterpret_cast<const char*>(
        bytes.data() + record->payload_offset);
    output.assign(begin, record->payload_bytes);
    // The game's string reader keeps the text before the first NUL.
    const auto nul = output.find('\0');
    if (nul != std::string::npos) output.resize(nul);
    return true;
}

const MobChunkRecord* unique_field(
    const std::unordered_map<std::uint32_t, const MobChunkRecord*>& fields,
    std::uint32_t id) noexcept {
    const auto found = fields.find(id);
    return found == fields.end() ? nullptr : found->second;
}

bool exact_field_size(const MobChunkRecord* field, std::uint32_t expected) noexcept {
    return field && field->payload_bytes == expected;
}

void extract_static_objects(const std::vector<std::uint8_t>& bytes,
                            const std::vector<MobChunkRecord>& chunks,
                            std::vector<StaticObjectSource>& objects,
                            bool include_levers) {
    for (const MobChunkRecord& chunk : chunks) {
        // A lever chunk carries the common object fields directly, beside its
        // own lever parameters (state count, science, door flag). The server
        // applies the MOB complection after the lever parameters, so the
        // common fields alone give its authored appearance.
        const bool lever = include_levers && chunk.id == kMobLever;
        if (chunk.id == kMobObject || lever) {
            StaticObjectSource object{};
            // The game reads fields in order: a repeated field keeps the last
            // value, and an unknown field id is skipped (recorded here for
            // diagnostics only).
            std::unordered_map<std::uint32_t, const MobChunkRecord*> fields;
            for (const MobChunkRecord& field : chunk.children) {
                fields.insert_or_assign(field.id, &field);
                if (!known_object_field(field.id) &&
                    !(lever && (field.id >> 16) == (kMobLever >> 16)))
                    object.unknown_field_ids.push_back(field.id);
            }
            const auto* nid = unique_field(fields, kFieldNid);
            const auto* type = unique_field(fields, kFieldType);
            const auto* owner = unique_field(fields, kFieldOwner);
            const auto* position = unique_field(fields, kFieldPosition);
            const auto* quaternion = unique_field(fields, kFieldQuaternion);
            const auto* shape = unique_field(fields, kFieldShape);
            const auto* quest = unique_field(fields, kFieldQuest);
            const auto* shadow = unique_field(fields, kFieldShadow);
            const auto* parent_nid = unique_field(fields, kFieldParentNid);
            const auto* parts = unique_field(fields, kFieldParts);
            // A missing field takes the game's constructor default; a field of
            // the wrong size puts the game's reader out of step (rejected).
            // A missing type makes the game dereference a null object.
            const auto sized = [](const MobChunkRecord* field, std::uint32_t expected) {
                return !field || field->payload_bytes == expected;
            };
            bool malformed = !exact_field_size(type, 4) || !sized(nid, 4) ||
                !sized(owner, 1) || !sized(position, 12) ||
                !sized(quaternion, 16) || !sized(shape, 12) || !sized(quest, 1) ||
                !sized(shadow, 1) || !sized(parent_nid, 4) ||
                (parts && parts->payload_bytes < 4);
            malformed = malformed ||
                !legacy_string(bytes, unique_field(fields, kFieldName), object.name) ||
                !legacy_string(bytes, unique_field(fields, kFieldModel),
                               object.visual_reference) ||
                !legacy_string(bytes, unique_field(fields, kFieldTexture0),
                               object.primary_texture) ||
                !legacy_string(bytes, unique_field(fields, kFieldTexture1),
                               object.secondary_texture) ||
                !legacy_string(bytes, unique_field(fields, kFieldParentName),
                               object.prototype_reference);
            std::uint32_t part_count = 0;
            if (!malformed) {
                // Publish the same ASCII keys used by asset scheduling and
                // resolution. MobDocument retains the original source bytes.
                object.visual_reference = fold_ascii(std::move(object.visual_reference));
                object.primary_texture = fold_ascii(std::move(object.primary_texture));
                object.secondary_texture = fold_ascii(std::move(object.secondary_texture));
                // Game constructor defaults: NID and parent -1, owner 0,
                // use-in-script false, shadow true; the two flags are set
                // only by a stored byte of exactly 1.
                object.nid = nid ? read_u32(bytes.data() + nid->payload_offset) : UINT32_MAX;
                object.family = read_u32(bytes.data() + type->payload_offset);
                object.owner = owner ? bytes[owner->payload_offset] : 0u;
                // The game marks the object synchronised (flags 0x20000, game.exe
                // 005927C0) for use-in-script or for a non-empty script name
                // (B016): scripts address either, so neither is immutable.
                std::string script_name;
                (void)legacy_string(bytes, unique_field(fields, kFieldQuestInfo), script_name);
                object.quest = (quest && bytes[quest->payload_offset] == 1u) || !script_name.empty() ? 1u : 0u;
                object.shadow = !shadow || bytes[shadow->payload_offset] == 1u ? 1u : 0u;
                object.prototype_nid = parent_nid ? read_u32(
                    bytes.data() + parent_nid->payload_offset) : UINT32_MAX;
                part_count = parts ? read_u32(bytes.data() + parts->payload_offset) : 0u;
                // Entries are nested B00D strings: id, total size, name bytes.
                std::uint32_t cursor = parts ? parts->payload_offset + 4u : 0u;
                const std::uint32_t end = parts ? parts->payload_offset + parts->payload_bytes : 0u;
                for (std::uint32_t index = 0; index < part_count && !malformed; ++index) {
                    if (end - cursor < 8u || read_u32(bytes.data() + cursor) != kFieldParts) {
                        malformed = true;
                        break;
                    }
                    const std::uint32_t total = read_u32(bytes.data() + cursor + 4u);
                    if (total < 8u || total > end - cursor) {
                        malformed = true;
                        break;
                    }
                    std::string name(reinterpret_cast<const char*>(bytes.data() + cursor + 8u), total - 8u);
                    const auto nul = name.find('\0');
                    if (nul != std::string::npos) name.resize(nul);
                    // An empty part name is a fatal "body part not found" in the game.
                    malformed = name.empty();
                    object.part_names.push_back(fold_ascii(std::move(name)));
                    cursor += total;
                }
                // A count that does not consume the list puts the game out of step.
                malformed = malformed || cursor != end;
                // Defaults: position (0,0,0), rotation w=1, complection 0.5.
                object.position = {};
                object.quaternion = {{1.0f, 0.0f, 0.0f, 0.0f}};
                object.shape = {{0.5f, 0.5f, 0.5f}};
                if (position)
                    for (std::size_t index = 0; index < object.position.size(); ++index)
                        object.position[index] = read_float(
                            bytes.data() + position->payload_offset + index * 4);
                if (quaternion)
                    for (std::size_t index = 0; index < object.quaternion.size(); ++index)
                        object.quaternion[index] = read_float(
                            bytes.data() + quaternion->payload_offset + index * 4);
                // MOB B00C serializes grace,strength,tall. Convert once at the
                // resource boundary to the strength,grace,tall selector used
                // by FIG8 positions/bounds and BON endpoints everywhere else.
                if (shape) {
                    const std::array<float, 3> raw_shape{{
                        read_float(bytes.data() + shape->payload_offset),
                        read_float(bytes.data() + shape->payload_offset + 4u),
                        read_float(bytes.data() + shape->payload_offset + 8u)}};
                    object.shape = {{raw_shape[1], raw_shape[0], raw_shape[2]}};
                }
                // The game sets these unchecked; a non-finite value takes its
                // default so the GPU never sees it.
                if (!finite(object.position) || !finite(object.quaternion) ||
                    !finite(object.shape)) {
                    if (!finite(object.position)) object.position = {};
                    if (!finite(object.quaternion)) object.quaternion = {{1.0f, 0.0f, 0.0f, 0.0f}};
                    if (!finite(object.shape)) object.shape = {{0.5f, 0.5f, 0.5f}};
                    char detail[128];
                    std::snprintf(detail, sizeof(detail), "MOB object %u: non-finite placement reset to defaults",
                                  object.nid);
                    bridge_append_probe_event("mob_object_repaired", detail);
                }
            }
            std::sort(object.unknown_field_ids.begin(),
                      object.unknown_field_ids.end());
            object.unknown_field_ids.erase(
                std::unique(object.unknown_field_ids.begin(),
                            object.unknown_field_ids.end()),
                object.unknown_field_ids.end());
            if (malformed)
                object.rejection = StaticObjectRejection::malformed;
            else if (object.quest)
                object.rejection = StaticObjectRejection::script_mutable;
            else if (part_count)
                object.rejection = StaticObjectRejection::multipart;
            else if (object.visual_reference.empty())
                object.rejection = StaticObjectRejection::missing_visual;
            else if (object.primary_texture.empty())
                object.rejection = StaticObjectRejection::missing_texture;
            objects.push_back(std::move(object));
        }
        extract_static_objects(bytes, chunk.children, objects, include_levers);
    }
}

bool parse_lnk(const ResourceBlob& blob, std::vector<ModelLink>& links,
               ResourceFailure& failure) {
    if (!blob.bytes || blob.bytes->size() < 4) {
        fail(failure, ResourceFailureCode::parse, "LNK header is truncated");
        return false;
    }
    const auto& bytes = *blob.bytes;
    const std::uint32_t count = read_u32(bytes.data());
    // Every link needs two length words. The game accepts any count that its
    // data can hold, zero included (nothing is drawn).
    if (count > (bytes.size() - 4) / 8u) {
        fail(failure, ResourceFailureCode::parse, "LNK link count exceeds its data");
        return false;
    }
    std::size_t cursor = 4;
    std::unordered_map<std::string, std::uint32_t> prior;
    std::uint32_t dropped = 0;
    try { links.reserve(count); } catch (...) {
        fail(failure, ResourceFailureCode::parse, "LNK allocation failed");
        return false;
    }
    // The game copies the length-prefixed bytes and appends a NUL, so a name
    // ends at its first NUL.
    auto read_name = [&](std::string& output) {
        if (cursor + 4 > bytes.size()) return false;
        const std::uint32_t length = read_u32(bytes.data() + cursor);
        cursor += 4;
        if (length > bytes.size() - cursor) return false;
        output.assign(reinterpret_cast<const char*>(bytes.data() + cursor), length);
        cursor += length;
        const auto nul = output.find('\0');
        if (nul != std::string::npos) output.resize(nul);
        return true;
    };
    for (std::uint32_t index = 0; index < count; ++index) {
        ModelLink link{};
        if (!read_name(link.child) || !read_name(link.parent)) {
            fail(failure, ResourceFailureCode::parse,
                 "LNK contains a truncated name");
            return false;
        }
        // The game refuses a repeated (or unnamed) node as a child and as a
        // geometry range, so it is never positioned or drawn: drop it.
        const std::string child_key = fold_ascii(link.child);
        if (link.child.empty() ||
            !prior.emplace(child_key, static_cast<std::uint32_t>(links.size())).second) {
            ++dropped;
            continue;
        }
        if (!link.parent.empty()) {
            const auto found = prior.find(fold_ascii(link.parent));
            // The shipped loader links a node only when its named parent has
            // already been added. A missing or later parent leaves this node
            // unattached; the declared name remains available for diagnostics.
            // With no root at all the game leaves every node unplaced; the
            // nodes are kept the same way.
            if (found != prior.end()) link.parent_index = found->second;
        }
        links.push_back(std::move(link));
    }
    // Bytes after the declared links are never read by the game.
    if (dropped) {
        char detail[224];
        std::snprintf(detail, sizeof(detail), "%s: %u repeated or unnamed LNK nodes dropped, as the game does",
                      blob.identity.canonical_name.c_str(), dropped);
        bridge_append_probe_event("lnk_nodes_dropped", detail);
    }
    failure = {};
    return true;
}

std::uint64_t filetime_ticks(const FILETIME& value) noexcept {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

std::uint64_t current_thread_cpu_ticks() noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return 0;
    return filetime_ticks(kernel) + filetime_ticks(user);
}

double milliseconds(std::int64_t ticks, std::int64_t frequency) noexcept {
    return frequency > 0 ? 1000.0 * static_cast<double>(ticks) /
        static_cast<double>(frequency) : 0.0;
}

std::uint64_t hash_bytes(std::uint64_t hash, const void* data,
                         std::size_t bytes) noexcept {
    const auto* source = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < bytes; ++index) {
        hash ^= source[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t hash_string(std::uint64_t hash, const std::string& value) noexcept {
    hash = hash_bytes(hash, value.data(), value.size());
    const std::uint8_t separator = 0xff;
    return hash_bytes(hash, &separator, 1);
}

std::uint64_t snapshot_fingerprint(const OwnedMapSnapshot& snapshot) noexcept {
    // Diagnostic of deterministic publication metadata, never an asset identity
    // or payload scan. Independent immutable opens intentionally have distinct IDs.
    const auto resource_description = [](std::uint64_t value, const ResourceIdentity& resource) {
        value = hash_string(value, resource.canonical_name);
        value = hash_bytes(value, &resource.absolute_offset, sizeof(resource.absolute_offset));
        value = hash_bytes(value, &resource.bytes, sizeof(resource.bytes));
        value = hash_bytes(value, &resource.mount_ordinal, sizeof(resource.mount_ordinal));
        return hash_bytes(value, &resource.loose_file, sizeof(resource.loose_file));
    };
    std::uint64_t hash = 1469598103934665603ull;
    hash = hash_bytes(hash, &snapshot.generation, sizeof(snapshot.generation));
    hash = hash_string(hash, snapshot.map_resource);
    hash = hash_string(hash, snapshot.terrain_resource);
    if (snapshot.terrain) {
        hash = resource_description(hash, snapshot.terrain->identity);
        hash = hash_string(hash, snapshot.terrain->map_header_resource);
        hash = hash_bytes(hash, &snapshot.terrain->maximum_z,
                          sizeof(snapshot.terrain->maximum_z));
        hash = hash_bytes(hash, &snapshot.terrain->sector_count_x,
                          sizeof(snapshot.terrain->sector_count_x));
        hash = hash_bytes(hash, &snapshot.terrain->sector_count_y,
                          sizeof(snapshot.terrain->sector_count_y));
    }
    for (const StaticObjectSource& object : snapshot.objects) {
        hash = hash_bytes(hash, &object.nid, sizeof(object.nid));
        hash = hash_bytes(hash, &object.family, sizeof(object.family));
        hash = hash_bytes(hash, &object.prototype_nid,
                          sizeof(object.prototype_nid));
        hash = hash_string(hash, object.prototype_reference);
        hash = hash_string(hash, object.visual_reference);
        hash = hash_string(hash, object.primary_texture);
        hash = hash_bytes(hash, object.position.data(), sizeof(object.position));
        hash = hash_bytes(hash, object.quaternion.data(), sizeof(object.quaternion));
        hash = hash_bytes(hash, object.shape.data(), sizeof(object.shape));
        hash = hash_bytes(hash, &object.rejection, sizeof(object.rejection));
    }
    for (const auto& item : snapshot.models) {
        hash = hash_string(hash, item.first);
        hash = resource_description(hash, item.second->model_container);
        for (const ModelNodeCpuAsset& node : item.second->nodes) {
            hash = hash_string(hash, node.name);
            hash = hash_bytes(hash, &node.figure.signature,
                              sizeof(node.figure.signature));
            hash = hash_bytes(hash, &node.figure.material_group,
                              sizeof(node.figure.material_group));
        }
    }
    for (const auto& item : snapshot.textures) {
        hash = hash_string(hash, item.first);
        hash = resource_description(hash, item.second->identity);
        hash = hash_bytes(hash, &item.second->width, sizeof(item.second->width));
        hash = hash_bytes(hash, &item.second->height, sizeof(item.second->height));
    }
    return hash;
}

std::uint64_t owned_model_bytes(const ModelCpuAsset& model) noexcept {
    std::uint64_t total = sizeof(model);
    for (const ModelNodeCpuAsset& node : model.nodes) {
        total += node.figure.positions.size() * sizeof(NativePackedGroup);
        total += node.figure.normals.size() * sizeof(NativePackedGroup);
        total += node.figure.texture_coordinates.size() *
            sizeof(NativeTextureCoordinate);
        total += node.figure.indices.size() * sizeof(std::uint16_t);
        total += node.figure.components.size() * sizeof(NativeVertexComponent);
        total += node.figure.morph_components.size() *
            sizeof(FigureMorphComponent);
        total += node.figure.bounds.size() * sizeof(FigureBounds);
        total += node.figure.user_data.size();
        total += node.bone_positions.size() * sizeof(float);
    }
    return total;
}

std::string compact_failure(const char* kind, const std::string& name,
                            const ResourceFailure& failure) {
    std::string output(kind);
    output += ':';
    output += name;
    output += ':';
    output += resource_failure_code_name(failure.code);
    if (!failure.message.empty()) output += ":" + failure.message;
    return output;
}

std::string json_escape(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 8);
    for (unsigned char byte : input) {
        switch (byte) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20u) {
                static constexpr char digits[] = "0123456789abcdef";
                output += "\\u00";
                output += digits[byte >> 4];
                output += digits[byte & 15];
            } else {
                output.push_back(static_cast<char>(byte));
            }
        }
    }
    return output;
}

} // namespace

std::array<NativeTextureCoordinate, 9> terrain_tile_uvs(const TerrainCpuAsset& terrain,
    const TerrainTileDescriptor& tile) noexcept {
    std::uint16_t material = 0;
    std::uint32_t position = 0;
    if (!terrain.tiles_per_texture_side || !terrain.tile_size ||
        !terrain_material(tile.tile_index, terrain, material, position))
        return {};
    return terrain_uvs(tile.rotation, position, terrain, material >= terrain.texture_count);
}

const char* static_object_rejection_name(StaticObjectRejection reason) noexcept {
    switch (reason) {
    case StaticObjectRejection::none: return "none";
    case StaticObjectRejection::malformed: return "malformed";
    case StaticObjectRejection::duplicate_nid: return "duplicate_nid";
    case StaticObjectRejection::unknown_field: return "unknown_field";
    case StaticObjectRejection::script_mutable: return "script_mutable";
    case StaticObjectRejection::hierarchical: return "hierarchical";
    case StaticObjectRejection::multipart: return "multipart";
    case StaticObjectRejection::missing_visual: return "missing_visual";
    case StaticObjectRejection::missing_texture: return "missing_texture";
    case StaticObjectRejection::outside_asset_slice: return "outside_asset_slice";
    case StaticObjectRejection::animated_asset: return "animated_asset";
    case StaticObjectRejection::asset_failure: return "asset_failure";
    }
    return "unknown";
}

const std::string* figure_texture_for_type(
    const StaticObjectSource& source, std::uint32_t type_id) noexcept {
    const std::string* selected = nullptr;
    switch (type_id) {
    case 1: case 3: case 4: case 6: case 7: case 8:
    case 9: case 11: case 13: case 14: case 15: case 16:
        selected = &source.primary_texture;
        break;
    case 2: case 5: case 10: case 12:
        selected = &source.secondary_texture;
        break;
    default:
        return nullptr;
    }
    return selected->empty() ? nullptr : selected;
}

bool parse_mob_document(const ResourceBlob& blob, MobDocument& document,
                        std::vector<StaticObjectSource>& objects,
                        ResourceFailure& failure, bool include_levers) {
    document = {};
    objects.clear();
    if (!blob.bytes || blob.bytes->empty() ||
        blob.bytes->size() > (std::numeric_limits<std::uint32_t>::max)()) {
        fail(failure, ResourceFailureCode::parse,
             "MOB is empty or exceeds the 32-bit format boundary");
        return false;
    }
    document.identity = blob.identity;
    document.bytes = blob.bytes;
    if (!parse_mob_chunks(*blob.bytes, 0,
                          static_cast<std::uint32_t>(blob.bytes->size()), 0,
                          document.roots, failure))
        return false;
    extract_static_objects(*blob.bytes, document.roots, objects, include_levers);
    // The game stores objects in a map keyed by NID: the last object in file
    // order with a given NID is created and earlier ones never are. Order by
    // NID keeping file order (stable), then retire all but the last of each.
    std::stable_sort(objects.begin(), objects.end(), [](const auto& left, const auto& right) {
        return left.nid < right.nid;
    });
    for (std::size_t begin = 0; begin < objects.size();) {
        std::size_t end = begin + 1;
        while (end < objects.size() && objects[end].nid == objects[begin].nid) ++end;
        for (std::size_t index = begin; index + 1 < end; ++index)
            objects[index].rejection = StaticObjectRejection::duplicate_nid;
        begin = end;
    }
    failure = {};
    return true;
}

bool parse_figure_asset(const ResourceBlob& blob, FigureCpuAsset& figure,
                        ResourceFailure& failure) {
    figure = {};
    if (!blob.bytes || blob.bytes->size() < 40) {
        fail(failure, ResourceFailureCode::parse, "FIG header is truncated");
        return false;
    }
    const auto& bytes = *blob.bytes;
    // The game tests only the "FIG" prefix: exactly "FIG8" has eight shapes
    // and every other fourth byte is read as one shape.
    const std::uint32_t signature = read_u32(bytes.data());
    const std::uint32_t shape_count = (signature & 0x00ffffffu) != (kFig1 & 0x00ffffffu) ? 0u :
        signature == kFigureSignature8 ? 8u : 1u;
    if (!shape_count) {
        // CFigureMapLoaded::LoadOldFigure: six corners (weak/strong/fat,
        // short/tall), counted channels and an unused 52-byte CFace stream.
        // Its interpolation is bilinear in strength/grace and linear in tall.
        // Duplicate each fat corner into both strength corners at grace=1;
        // this gives exactly the same polynomial in the eight-corner layout.
        // This is a format decoder, with the same channel validation below.
        try {
            std::vector<std::uint8_t> converted(40,0);
            std::size_t cursor=0;
            const auto take=[&](std::size_t count) {
                if(cursor>bytes.size()||count>bytes.size()-cursor)throw std::runtime_error("headerless FIG channel is truncated");
                const auto first=cursor;cursor+=count;return first;
            };
            const auto count=[&]() {const auto at=take(4);return read_u32(bytes.data()+at);};
            const auto set=[&](std::size_t at,std::uint32_t value) {
                for(unsigned i=0;i<4;++i)converted[at+i]=static_cast<std::uint8_t>(value>>(i*8));
            };
            const auto append=[&](std::size_t at,std::size_t size) {
                if(size)converted.insert(converted.end(),bytes.begin()+at,bytes.begin()+at+size);
            };
            const auto material=count(),texture=count();
            set(0,kFigureSignature8);set(32,material);set(36,texture);
            constexpr unsigned corner[]={0,1,2,2,3,4,5,5};
            for(unsigned channel=0;channel<4;++channel) {
                const std::size_t width=channel==3?4:12;const auto first=take(6*width);
                for(unsigned s:corner)append(first+s*width,width);
            }
            const auto positions=count();set(4,positions);
            if(positions>(bytes.size()-cursor)/(6*12*4))throw std::runtime_error("headerless FIG position count is invalid");
            for(unsigned group=0;group<positions;++group)for(unsigned axis=0;axis<3;++axis) {
                const auto first=take(6*4*4);for(unsigned s:corner)append(first+s*16,16);
            }
            const auto normals=count();set(8,normals);auto first=take(std::size_t(normals)*64);append(first,std::size_t(normals)*64);
            const auto uv=count();set(12,uv);first=take(std::size_t(uv)*8);append(first,std::size_t(uv)*8);
            const auto faces=count();take(std::size_t(faces)*52);
            const auto components=count();set(20,components);const auto componentBytes=std::size_t(components)*6;const auto componentAt=take(componentBytes);
            const auto morphs=count();set(24,morphs);const auto morphBytes=std::size_t(morphs)*4;const auto morphAt=take(morphBytes);
            const auto indices=count();set(16,indices);const auto indexBytes=std::size_t(indices)*2;first=take(indexBytes);
            append(first,indexBytes);append(componentAt,componentBytes);append(morphAt,morphBytes);
            ResourceBlob decoded=blob;decoded.bytes=std::make_shared<const std::vector<std::uint8_t>>(std::move(converted));
            return parse_figure_asset(decoded,figure,failure);
        } catch(const std::exception& e) {
            fail(failure,ResourceFailureCode::parse,e.what());return false;
        }
    }
    const std::uint32_t position_groups = read_u32(bytes.data() + 4);
    const std::uint32_t normal_groups = read_u32(bytes.data() + 8);
    const std::uint32_t texture_count = read_u32(bytes.data() + 12);
    const std::uint32_t index_count = read_u32(bytes.data() + 16);
    const std::uint32_t component_count = read_u32(bytes.data() + 20);
    const std::uint32_t morph_count = read_u32(bytes.data() + 24);
    const std::uint32_t user_offset = read_u32(bytes.data() + 28);
    const std::uint32_t material_group = read_u32(bytes.data() + 32);
    const std::uint32_t texture_number = read_u32(bytes.data() + 36);

    std::uint64_t bound_floats = 0;
    std::uint64_t position_floats = 0;
    std::uint64_t normal_floats = 0;
    std::uint64_t texture_floats = 0;
    std::uint64_t index_bytes = 0;
    std::uint64_t component_bytes = 0;
    std::uint64_t morph_bytes = 0;
    std::uint64_t required = 40;
    if (!checked_multiply(shape_count, 10u, bound_floats) ||
        !checked_multiply(static_cast<std::uint64_t>(shape_count) * 12u,
                          position_groups, position_floats) ||
        !checked_multiply(16u, normal_groups, normal_floats) ||
        !checked_multiply(texture_count, 2u, texture_floats) ||
        !checked_multiply(index_count, 2u, index_bytes) ||
        !checked_multiply(component_count, 6u, component_bytes) ||
        !checked_multiply(morph_count, 4u, morph_bytes)) {
        fail(failure, ResourceFailureCode::parse, "FIG count arithmetic overflowed");
        return false;
    }
    const std::uint64_t float_bytes =
        (bound_floats + position_floats + normal_floats + texture_floats) * 4u;
    if (!checked_add(required, float_bytes, required) ||
        !checked_add(required, index_bytes, required) ||
        !checked_add(required, component_bytes, required) ||
        !checked_add(required, morph_bytes, required) || required > bytes.size()) {
        fail(failure, ResourceFailureCode::parse,
             "FIG channels do not fit the declared resource bytes");
        return false;
    }
    // The game never reads the user-data offset and ignores trailing bytes.
    // Keep user data only where the offset actually points behind the channels.
    const bool user_data_valid = user_offset >= required && user_offset <= bytes.size();
    std::vector<std::uint8_t> position_used;
    try {
        position_used.resize(static_cast<std::size_t>(position_groups) * 4u);
        figure.bounds.resize(shape_count);
        figure.positions.resize(static_cast<std::size_t>(shape_count) *
                                position_groups);
        figure.normals.resize(normal_groups);
        figure.texture_coordinates.resize(texture_count);
        figure.indices.resize(index_count);
        figure.components.resize(component_count);
        figure.morph_components.resize(morph_count);
    } catch (...) {
        fail(failure, ResourceFailureCode::parse, "FIG allocation failed");
        return false;
    }
    figure.identity = blob.identity;
    // Publish the layout the game reads ("FIG2".."FIG7" etc. are one shape).
    figure.signature = shape_count == 8u ? kFigureSignature8 : kFig1;
    if (figure.signature != signature)
        bridge_append_probe_event("figure_signature_variant",
            (blob.identity.canonical_name + ": FIG variant read as one shape, as the game does").c_str());
    figure.shape_count = shape_count;
    figure.position_groups = position_groups;
    figure.normal_groups = normal_groups;
    figure.material_group = material_group;
    figure.texture_number = texture_number;

    std::size_t cursor = 40;
    std::array<bool, 8> bounds_rebuilt{};
    std::uint32_t repaired_texture_coordinates = 0, repaired_positions = 0;
    const std::size_t center = cursor;
    const std::size_t minimum = center + shape_count * 3u * 4u;
    const std::size_t maximum = minimum + shape_count * 3u * 4u;
    const std::size_t radius = maximum + shape_count * 3u * 4u;
    for (std::uint32_t shape_index = 0; shape_index < shape_count; ++shape_index) {
        FigureBounds& bounds = figure.bounds[shape_index];
        for (std::uint32_t component = 0; component < 3; ++component) {
            bounds.center[component] = read_float(
                bytes.data() + center + (shape_index * 3u + component) * 4u);
            bounds.minimum[component] = read_float(
                bytes.data() + minimum + (shape_index * 3u + component) * 4u);
            bounds.maximum[component] = read_float(
                bytes.data() + maximum + (shape_index * 3u + component) * 4u);
        }
        bounds.radius = read_float(bytes.data() + radius + shape_index * 4u);
        // The game's draw path skips the stored bounds and recomputes them
        // from the vertices; a non-finite stored value is rebuilt from the
        // positions below.
        bounds_rebuilt[shape_index] = !finite(bounds.center) || !finite(bounds.minimum) ||
            !finite(bounds.maximum) || !std::isfinite(bounds.radius);
    }
    cursor += static_cast<std::size_t>(bound_floats * 4u);
    cursor += static_cast<std::size_t>(position_floats * 4u);
    std::uint32_t repaired_normals = 0;
    for (std::uint32_t group = 0; group < normal_groups; ++group) {
        const std::size_t group_base = cursor +
            static_cast<std::size_t>(group) * 16u * 4u;
        NativePackedGroup& target = figure.normals[group];
        for (std::uint32_t lane = 0; lane < 4; ++lane) {
            target.x[lane] = read_float(bytes.data() + group_base + lane * 4u);
            target.y[lane] = read_float(bytes.data() + group_base +
                                        (4u + lane) * 4u);
            target.z[lane] = read_float(bytes.data() + group_base +
                                        (8u + lane) * 4u);
            // The game lights with XYZ only and never validates the packed
            // normal, so neither may we: W (1 in shipped figures, 0 in some
            // tool exports) is ignored and stored as 1, and a non-finite
            // direction becomes straight up instead of rejecting the model.
            target.w[lane] = 1.0f;
            if (!std::isfinite(target.x[lane]) ||
                !std::isfinite(target.y[lane]) ||
                !std::isfinite(target.z[lane])) {
                target.x[lane] = 0.0f; target.y[lane] = 0.0f; target.z[lane] = 1.0f;
                ++repaired_normals;
            }
        }
    }
    cursor += static_cast<std::size_t>(normal_floats * 4u);
    if (repaired_normals) {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%s: %u non-finite packed normals drawn as up",
                      blob.identity.canonical_name.c_str(), repaired_normals);
        bridge_append_probe_event("figure_normals_repaired", detail);
    }
    for (std::uint32_t index = 0; index < texture_count; ++index) {
        figure.texture_coordinates[index].u = read_float(
            bytes.data() + cursor + static_cast<std::size_t>(index) * 8u);
        figure.texture_coordinates[index].v = read_float(
            bytes.data() + cursor + static_cast<std::size_t>(index) * 8u + 4u);
        // Unchecked by the game; a non-finite coordinate samples the corner.
        if (!std::isfinite(figure.texture_coordinates[index].u) ||
            !std::isfinite(figure.texture_coordinates[index].v)) {
            figure.texture_coordinates[index].u = 0.0f;
            figure.texture_coordinates[index].v = 0.0f;
            ++repaired_texture_coordinates;
        }
    }
    cursor += static_cast<std::size_t>(texture_floats * 4u);
    for (std::uint32_t index = 0; index < index_count; ++index) {
        figure.indices[index] = read_u16(bytes.data() + cursor + index * 2u);
        if (figure.indices[index] >= component_count) {
            fail(failure, ResourceFailureCode::parse,
                 "FIG topology references a missing vertex component");
            return false;
        }
    }
    cursor += static_cast<std::size_t>(index_bytes);
    const std::uint64_t position_count = static_cast<std::uint64_t>(position_groups) * 4u;
    const std::uint64_t normal_count = static_cast<std::uint64_t>(normal_groups) * 4u;
    for (std::uint32_t index = 0; index < component_count; ++index) {
        NativeVertexComponent& component = figure.components[index];
        component.position = read_u16(bytes.data() + cursor + index * 6u);
        component.normal = read_u16(bytes.data() + cursor + index * 6u + 2u);
        component.texture = read_u16(bytes.data() + cursor + index * 6u + 4u);
        if (component.position >= position_count || component.normal >= normal_count ||
            component.texture >= texture_count) {
            fail(failure, ResourceFailureCode::parse,
                 "FIG vertex component references a missing channel");
            return false;
        }
    }
    for (const auto& component : figure.components)
        position_used[component.position] = 1;
    cursor += static_cast<std::size_t>(component_bytes);
    for (std::uint32_t index = 0; index < morph_count; ++index) {
        FigureMorphComponent& component = figure.morph_components[index];
        component.position = read_u16(bytes.data() + cursor + index * 4u);
        component.normal = read_u16(bytes.data() + cursor + index * 4u + 2u);
        if (component.position >= position_count || component.normal >= normal_count) {
            fail(failure, ResourceFailureCode::parse,
                 "FIG morph declaration references a missing channel");
            return false;
        }
    }
    for (const auto& component : figure.morph_components)
        position_used[component.position] = 1;

    // Packed groups can end in unused lanes containing arbitrary padding,
    // including NaNs in shipped FIG8 data. Only the validated component
    // streams establish which positions belong to the geometry. Keep padding
    // finite for downstream group operations and out of the render envelope.
    cursor = 40u + static_cast<std::size_t>(bound_floats * 4u);
    for (std::uint32_t shape_index = 0; shape_index < shape_count; ++shape_index)
        if (bounds_rebuilt[shape_index]) {
            figure.bounds[shape_index].minimum.fill((std::numeric_limits<float>::max)());
            figure.bounds[shape_index].maximum.fill(std::numeric_limits<float>::lowest());
        }
    for (std::uint32_t group = 0; group < position_groups; ++group) {
        const std::size_t group_base = cursor +
            static_cast<std::size_t>(group) * shape_count * 12u * 4u;
        for (std::uint32_t shape_index = 0; shape_index < shape_count;
             ++shape_index) {
            NativePackedGroup& target = figure.positions[
                static_cast<std::size_t>(shape_index) * position_groups + group];
            for (std::uint32_t lane = 0; lane < 4; ++lane) {
                if (!position_used[static_cast<std::size_t>(group) * 4u + lane]) {
                    target.x[lane] = target.y[lane] = target.z[lane] = 0.0f;
                    target.w[lane] = 1.0f;
                    continue;
                }
                target.x[lane] = read_float(bytes.data() + group_base +
                    (shape_index * 4u + lane) * 4u);
                target.y[lane] = read_float(bytes.data() + group_base +
                    (shape_count * 4u + shape_index * 4u + lane) * 4u);
                target.z[lane] = read_float(bytes.data() + group_base +
                    (shape_count * 8u + shape_index * 4u + lane) * 4u);
                target.w[lane] = 1.0f;
                // Unchecked by the game; a non-finite used position is drawn
                // at the node origin.
                if (!std::isfinite(target.x[lane]) ||
                    !std::isfinite(target.y[lane]) ||
                    !std::isfinite(target.z[lane])) {
                    target.x[lane] = target.y[lane] = target.z[lane] = 0.0f;
                    ++repaired_positions;
                }
                const double x = target.x[lane], y = target.y[lane], z = target.z[lane];
                float extent = std::nextafter(
                    static_cast<float>(std::sqrt(x*x+y*y+z*z)), INFINITY);
                if (!std::isfinite(extent)) extent = (std::numeric_limits<float>::max)();
                figure.shape_position_radii[shape_index] = (std::max)(
                    figure.shape_position_radii[shape_index], extent);
                if (bounds_rebuilt[shape_index]) {
                    FigureBounds& bounds = figure.bounds[shape_index];
                    const std::array<float, 3> point{{target.x[lane], target.y[lane], target.z[lane]}};
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        bounds.minimum[axis] = (std::min)(bounds.minimum[axis], point[axis]);
                        bounds.maximum[axis] = (std::max)(bounds.maximum[axis], point[axis]);
                    }
                }
            }
        }
    }
    std::uint32_t rebuilt_bounds = 0;
    for (std::uint32_t shape_index = 0; shape_index < shape_count; ++shape_index) {
        if (!bounds_rebuilt[shape_index]) continue;
        ++rebuilt_bounds;
        FigureBounds& bounds = figure.bounds[shape_index];
        if (bounds.minimum[0] > bounds.maximum[0]) {  // no used position
            bounds.minimum.fill(0.0f);
            bounds.maximum.fill(0.0f);
        }
        double squared = 0.0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            bounds.center[axis] = 0.5f * (bounds.minimum[axis] + bounds.maximum[axis]);
            const double half = 0.5 * (double(bounds.maximum[axis]) - bounds.minimum[axis]);
            squared += half * half;
        }
        bounds.radius = std::nextafter(static_cast<float>(std::sqrt(squared)), INFINITY);
        if (!finite(bounds.center) || !std::isfinite(bounds.radius)) {
            bounds.center.fill(0.0f);
            bounds.radius = figure.shape_position_radii[shape_index];
        }
    }
    if (repaired_texture_coordinates || repaired_positions || rebuilt_bounds) {
        char detail[224];
        std::snprintf(detail, sizeof(detail),
                      "%s: %u non-finite UVs, %u non-finite positions zeroed, %u shape bounds rebuilt",
                      blob.identity.canonical_name.c_str(), repaired_texture_coordinates,
                      repaired_positions, rebuilt_bounds);
        bridge_append_probe_event("figure_values_repaired", detail);
    }
    if (user_offset && user_data_valid) {
        try {
            figure.user_data.assign(bytes.begin() + user_offset, bytes.end());
        } catch (...) {
            fail(failure, ResourceFailureCode::parse,
                 "FIG user-data allocation failed");
            return false;
        }
    }
    failure = {};
    return true;
}

bool texture_alpha_pixels(const TextureCpuAsset& texture, std::size_t index,
                          TextureAlphaPixels& input) noexcept {
    input = {};
    // A core-mode stand-in (TextureCpuAsset::alpha_cut_mask): mip 0 only.
    if (!texture.source && index == 0 && !texture.mips.empty() &&
        (texture.alpha_cut_mask || (texture.opaque_mips & 1u))) {
        input.format = TextureAlphaFormat::cut_bits;
        input.width = texture.mips[0].width; input.height = texture.mips[0].height;
        if (texture.alpha_cut_mask) {
            input.data = texture.alpha_cut_mask->data();
            input.bytes = texture.alpha_cut_mask->size();
        }
        return true;
    }
    if (!texture.source || index >= texture.mips.size()) return false;
    switch (texture.format) {
    case MmpFormat::dxt1: input.format = TextureAlphaFormat::bc1; break;
    case MmpFormat::dxt3: input.format = TextureAlphaFormat::bc2; break;
    case MmpFormat::dxt5: input.format = TextureAlphaFormat::bc3; break;
    case MmpFormat::rgb565: input.pixel_bytes = 2; break;
    case MmpFormat::argb1555: input.pixel_bytes = 2; input.alpha_mask = 0x8000; break;
    case MmpFormat::argb4444: input.pixel_bytes = 2; input.alpha_mask = 0xf000; break;
    case MmpFormat::argb8888: input.pixel_bytes = 4; input.alpha_mask = 0xff000000; break;
    }
    const auto& mip = texture.mips[index];
    input.data = texture.source->data() + mip.byte_offset;
    input.bytes = mip.bytes; input.width = mip.width; input.height = mip.height;
    input.pitch = input.format == TextureAlphaFormat::packed ? mip.width * input.pixel_bytes :
        static_cast<std::uint32_t>(((std::uint64_t(mip.width) + 3) / 4) *
            (input.format == TextureAlphaFormat::bc1 ? 8 : 16));
    return true;
}

bool texture_rgb_pixels(const TextureCpuAsset& texture, std::size_t index, std::uint32_t& width,
                        std::uint32_t& height, std::vector<std::uint8_t>& rgb) noexcept try {
    width = height = 0; rgb.clear();
    if (!texture.source || index >= texture.mips.size()) return false;
    const auto& mip = texture.mips[index];
    if (mip.byte_offset > texture.source->size() || mip.bytes > texture.source->size() - mip.byte_offset ||
        !mip.width || !mip.height) return false;
    const std::uint8_t* data = texture.source->data() + mip.byte_offset;
    const std::uint32_t w = mip.width, h = mip.height;
    if (texture.format == MmpFormat::dxt1 || texture.format == MmpFormat::dxt3 || texture.format == MmpFormat::dxt5) {
        // The colour half of each block (DXT3/5: after their 8 alpha bytes).
        const unsigned block = texture.format == MmpFormat::dxt1 ? 8u : 16u;
        const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        if (mip.bytes < std::size_t(bw) * bh * block) return false;
        rgb.assign(std::size_t(w) * h * 3, 0);
        for (std::uint32_t by = 0; by < bh; ++by) for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint8_t* b = data + (std::size_t(by) * bw + bx) * block + (block - 8);
            const unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
            unsigned pal[4][3];
            const auto expand = [](unsigned c, unsigned* o) {
                o[0] = ((c >> 11) & 31) * 255 / 31; o[1] = ((c >> 5) & 63) * 255 / 63; o[2] = (c & 31) * 255 / 31;
            };
            expand(c0, pal[0]); expand(c1, pal[1]);
            for (int k = 0; k < 3; ++k) {
                // DXT3/5 colour blocks always use the four-colour mode.
                if (c0 > c1 || block == 16) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
                else { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
            }
            const std::uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | (std::uint32_t(b[7]) << 24);
            for (unsigned i = 0; i < 16; ++i) {
                const std::uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= w || y >= h) continue;
                const unsigned sel = (bits >> (2 * i)) & 3;
                for (int k = 0; k < 3; ++k) rgb[(std::size_t(y) * w + x) * 3 + k] = std::uint8_t(pal[sel][k]);
            }
        }
    } else {
        // Packed 16/32-bit texels; the masks are in the file's A, R, G, B order.
        // PNT3 is expanded at parse into BGRA bytes and keeps no masks.
        const unsigned bpp = texture.bit_count / 8;
        if ((bpp != 2 && bpp != 4) || mip.bytes < std::size_t(w) * h * bpp) return false;
        const bool bgra = texture.format_signature == 0x33544e50u;  // 'PNT3'
        if (!bgra)
            for (int c = 1; c < 4; ++c) if (!texture.masks[c] || !texture.channel_bits[c] || texture.channel_bits[c] > 8) return false;
        rgb.assign(std::size_t(w) * h * 3, 0);
        // Each channel's 8-bit value for every field value (at most 8 bits),
        // worked out once instead of a division per texel.
        // (A mask wider than its channel bits divides as before.)
        std::uint8_t expand[3][256]{};
        std::uint32_t top[3]{};
        if (!bgra)
            for (int c = 0; c < 3; ++c) {
                top[c] = (1u << texture.channel_bits[1 + c]) - 1;
                for (std::uint32_t f = 0; f <= top[c]; ++f) expand[c][f] = std::uint8_t(f * 255u / top[c]);
            }
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
            const std::uint8_t* q = data + i * bpp;
            if (bgra) { rgb[i * 3] = q[2]; rgb[i * 3 + 1] = q[1]; rgb[i * 3 + 2] = q[0]; continue; }
            const std::uint32_t v = bpp == 2 ? std::uint32_t(q[0] | (q[1] << 8)) :
                std::uint32_t(q[0] | (q[1] << 8) | (q[2] << 16)) | (std::uint32_t(q[3]) << 24);
            for (int c = 0; c < 3; ++c) {
                const std::uint32_t f = (v & texture.masks[1 + c]) >> texture.shifts[1 + c];
                rgb[i * 3 + c] = f <= top[c] ? expand[c][f] : std::uint8_t(f * 255u / top[c]);
            }
        }
    }
    width = w; height = h;
    return true;
} catch (...) { rgb.clear(); width = height = 0; return false; }

static void establish_texture_alpha(TextureCpuAsset& texture) noexcept {
    texture.opaque_mips = 0;
    TextureAlphaPixels input;
    for (std::size_t i = 0; i < texture.mips.size(); ++i)
        if (texture_alpha_pixels(texture, i, input) && texture_alpha_fully_opaque(input))
            texture.opaque_mips |= std::uint32_t{1} << i;
}

static bool parse_texture_asset_impl(const ResourceBlob& blob,
                                     TextureCpuAsset& texture,
                                     ResourceFailure& failure,
                                     TextureValueSource source = TextureValueSource::archive) {
    texture = {};
    const bool generated = source != TextureValueSource::archive;
    if (!blob.bytes || blob.bytes->size() < kMmpHeaderBytes) {
        fail(failure, ResourceFailureCode::parse, "MMP header is truncated");
        return false;
    }
    const auto& bytes = *blob.bytes;
    // The game's texture loader never compares the MMP signature; it only
    // writes it. Note a foreign one and read the header as the game does.
    if (read_u32(bytes.data()) != kMmp)
        bridge_append_probe_event("mmp_signature_ignored",
            (blob.identity.canonical_name + ": MMP signature differs; read as the game does").c_str());
    texture.identity = blob.identity;
    texture.width = read_u32(bytes.data() + 4);
    texture.height = read_u32(bytes.data() + 8);
    const std::uint32_t mip_count = read_u32(bytes.data() + 12);
    texture.format_signature = read_u32(bytes.data() + 16);
    texture.bit_count = read_u32(bytes.data() + 20);
    texture.masks = {read_u32(bytes.data() + 24), read_u32(bytes.data() + 36),
                     read_u32(bytes.data() + 48), read_u32(bytes.data() + 60)};
    texture.shifts = {read_u32(bytes.data() + 28), read_u32(bytes.data() + 40),
                      read_u32(bytes.data() + 52), read_u32(bytes.data() + 64)};
    texture.channel_bits = {
        read_u32(bytes.data() + 32), read_u32(bytes.data() + 44),
        read_u32(bytes.data() + 56), read_u32(bytes.data() + 68)};
    const std::uint32_t user_offset = read_u32(bytes.data() + 72);
    if (generated && (texture.format_signature != kArgb8888 ||
                      texture.bit_count != 32u || mip_count != 3u)) {
        fail(failure, ResourceFailureCode::parse,
             "generated MMP does not match the supported completed producer");
        return false;
    }
    if (!texture.width || !texture.height || !mip_count ||
        (texture.format_signature != kPnt3 && mip_count > 32u)) {
        fail(failure, ResourceFailureCode::parse,
             "MMP dimensions or mip count are invalid");
        return false;
    }
    std::uint32_t block_bytes = 0;
    switch (texture.format_signature) {
    case kDxt1:
        texture.format = MmpFormat::dxt1;
        block_bytes = 8;
        if (texture.bit_count != 4) {
            fail(failure, ResourceFailureCode::parse,
                 "MMP DXT1 bit count is inconsistent");
            return false;
        }
        break;
    case kDxt3:
        texture.format = MmpFormat::dxt3;
        block_bytes = 16;
        if (texture.bit_count != 8) {
            fail(failure, ResourceFailureCode::parse,
                 "MMP DXT3 bit count is inconsistent");
            return false;
        }
        break;
    case kDxt5:
        // The game uploads any DXTn the device exposes; D3D11 always has BC3.
        texture.format = MmpFormat::dxt5;
        block_bytes = 16;
        if (texture.bit_count != 8) {
            fail(failure, ResourceFailureCode::parse,
                 "MMP DXT5 bit count is inconsistent");
            return false;
        }
        break;
    case kPnt3:
        {
        std::uint64_t pixel_count = 0;
        if (texture.bit_count != 32u || mip_count == 0u || user_offset != 0u ||
            !checked_multiply(texture.width, texture.height, pixel_count) ||
            (pixel_count & 3u) != 0u ||
            mip_count > bytes.size() - kMmpHeaderBytes) {
            fail(failure, ResourceFailureCode::parse,
                 "MMP PNT3 compressed span is invalid");
            texture = {};
            return false;
        }
            const std::size_t encoded_offset = kMmpHeaderBytes;
            std::vector<std::uint8_t> decoded;
            if (encoded_offset + mip_count != bytes.size() ||
                !decode_pnt3_fresh_zero(bytes.data() + encoded_offset,
                                        mip_count, texture.width,
                                        texture.height, decoded)) {
                fail(failure, ResourceFailureCode::parse,
                     "MMP PNT3 stream is malformed or not exact");
                texture = {};
                return false;
            }
            std::shared_ptr<std::vector<std::uint8_t>> owned;
            try {
                owned = std::make_shared<std::vector<std::uint8_t>>(
                    std::move(decoded));
            } catch (...) {
                fail(failure, ResourceFailureCode::parse,
                     "MMP PNT3 output allocation failed");
                texture = {};
                return false;
            }
            texture.format = MmpFormat::argb8888;
            texture.mips.push_back({texture.width, texture.height, 0u,
                                    static_cast<std::uint32_t>(owned->size())});
            texture.source = std::move(owned);
            texture.texel_offset = 0u;
            establish_texture_alpha(texture);
            failure = {};
            return true;
        }
    case kRgb565: texture.format = MmpFormat::rgb565; break;
    case kArgb1555: texture.format = MmpFormat::argb1555; break;
    case kArgb4444: texture.format = MmpFormat::argb4444; break;
    case kArgb8888: texture.format = MmpFormat::argb8888; break;
    default:
        fail(failure, ResourceFailureCode::parse,
             "MMP pixel format is not established for this slice");
        return false;
    }
    if (!block_bytes && texture.bit_count != 16 && texture.bit_count != 32) {
        fail(failure, ResourceFailureCode::parse,
             "MMP uncompressed bit count is inconsistent");
        return false;
    }
    // Header +72 is the byte length of a user-data block right after the
    // header; level 0 starts at 76 + that length. This is the game's own
    // rule (SMMPFileHeader::ShiftToMipLevel) for archive and generated MMPs.
    std::uint64_t cursor = kMmpHeaderBytes;
    if (!checked_add(cursor, user_offset, cursor) || cursor > bytes.size()) {
        fail(failure, ResourceFailureCode::parse, "MMP user data is truncated");
        return false;
    }
    texture.texel_offset = static_cast<std::uint32_t>(cursor);
    std::uint32_t width = texture.width;
    std::uint32_t height = texture.height;
    try { texture.mips.reserve(mip_count); } catch (...) {
        fail(failure, ResourceFailureCode::parse, "MMP mip allocation failed");
        return false;
    }
    for (std::uint32_t level = 0; level < mip_count; ++level) {
        std::uint64_t level_bytes = 0;
        if (block_bytes) {
            const std::uint64_t columns = (std::max)(1u, (width + 3u) / 4u);
            const std::uint64_t rows = (std::max)(1u, (height + 3u) / 4u);
            if (!checked_multiply(columns, rows, level_bytes) ||
                !checked_multiply(level_bytes, block_bytes, level_bytes)) {
                fail(failure, ResourceFailureCode::parse,
                     "MMP block-compressed mip size overflowed");
                return false;
            }
        } else {
            if (!checked_multiply(width, height, level_bytes) ||
                !checked_multiply(level_bytes, texture.bit_count / 8u,
                                  level_bytes)) {
                fail(failure, ResourceFailureCode::parse,
                     "MMP uncompressed mip size overflowed");
                return false;
            }
        }
        std::uint64_t end = 0;
        if (!checked_add(cursor, level_bytes, end) || end > bytes.size() ||
            cursor > (std::numeric_limits<std::uint32_t>::max)() ||
            level_bytes > (std::numeric_limits<std::uint32_t>::max)()) {
            fail(failure, ResourceFailureCode::parse,
                 "MMP mip payload is truncated or exceeds 32-bit offsets");
            return false;
        }
        texture.mips.push_back({width, height, static_cast<std::uint32_t>(cursor),
                                static_cast<std::uint32_t>(level_bytes)});
        cursor = end;
        width = (std::max)(1u, width / 2u);
        height = (std::max)(1u, height / 2u);
    }
    const bool invalid_generated_size = source == TextureValueSource::generated_memory ?
        ((cursor + 3u) & ~std::uint64_t{3}) != bytes.size() : cursor != bytes.size();
    // The game never learns an archive member's size and ignores bytes after
    // the declared mips (shipped textures.res carries whole unused chains
    // behind a one-level header). Only the completed generated producer is
    // held to its exact size.
    if (generated && invalid_generated_size) {
        fail(failure, ResourceFailureCode::parse,
             "generated MMP size does not match its exact mip payload");
        return false;
    }
    if (user_offset) {
        try {
            texture.user_data.assign(bytes.begin() + kMmpHeaderBytes,
                bytes.begin() + kMmpHeaderBytes + user_offset);
        }
        catch (...) {
            fail(failure, ResourceFailureCode::parse,
                 "MMP user-data allocation failed");
            return false;
        }
    }
    texture.source = blob.bytes;
    texture.value_source = source;
    establish_texture_alpha(texture);
    failure = {};
    return true;
}

bool TerrainTexturePage::ensure_upload_bytes(ResourceFailure& failure, bool require_bc1_chain) const {
    if (texture) { failure = {}; return true; }
    ResourceBlob blob;
    if (!store || !store->read(location, blob, failure)) return false;
    auto loaded = std::make_shared<TextureCpuAsset>();
    if (!parse_texture_asset(blob, *loaded, failure)) return false;
    if ((require_bc1_chain && (loaded->format != MmpFormat::dxt1 || loaded->mips.size() < 2)) ||
        (mip_count && loaded->mips.size() != mip_count)) {
        fail(failure, ResourceFailureCode::parse, "terrain page has no matching BC1 mip chain");
        return false;
    }
    std::vector<NativeOwnedTextureMip> upload;
    upload.reserve(loaded->mips.size());
    for (const auto& mip : loaded->mips)
        upload.push_back({loaded->source->data() + mip.byte_offset,
                          mip.width, mip.height, mip.bytes});
    mip_count = static_cast<std::uint32_t>(upload.size());
    mips = std::move(upload);
    texture = std::move(loaded);
    failure = {};
    return true;
}

void TerrainTexturePage::release_upload_bytes() const noexcept {
    std::vector<NativeOwnedTextureMip>().swap(mips);
    texture.reset();
}

bool parse_texture_asset(const ResourceBlob& blob, TextureCpuAsset& texture,
                         ResourceFailure& failure) {
    TextureCpuAsset parsed{};
    try {
        if (parse_texture_asset_impl(blob, parsed, failure)) {
            texture = std::move(parsed);
            return true;
        }
    } catch (...) {
        fail(failure, ResourceFailureCode::parse,
             "MMP owned output allocation failed");
    }
    texture = {};
    return false;
}

std::uint16_t terrain_descriptor_index_count(
    std::uint8_t lod, std::uint8_t edge_mask, std::uint32_t stitch) noexcept {
    if (lod == 0u) {
        if (((stitch & 1u) && (edge_mask & 1u)) ||
            ((stitch & 2u) && (edge_mask & 2u)))
            return 21u;
        return 24u;
    }
    if (lod == 1u) {
        if (((stitch & 1u) && (edge_mask & 4u)) ||
            ((stitch & 2u) && (edge_mask & 8u)))
            return 9u;
        return 12u;
    }
    return 6u;
}

bool publish_terrain_sector_descriptors(
    TerrainCpuAsset& source, TerrainSectorCpuAsset& sector,
    std::uint32_t material_count) noexcept {
#ifndef EID3D11_TESTING
    (void)source;
#endif
    try {
        if (material_count > UINT16_MAX + 1u ||
            sector.tiles.size() > UINT16_MAX)
            return false;
        std::vector<std::uint16_t> counts(
            static_cast<std::size_t>(material_count) * 4u, 0u);
        for (const auto& tile : sector.tiles) {
#ifdef EID3D11_TESTING
            ++source.descriptor_classification_visits;
#endif
            if (tile.material_index >= material_count || tile.subpart >= 4u)
                return false;
            auto& count = counts[static_cast<std::size_t>(tile.material_index) *
                4u + tile.subpart];
            if (count == UINT16_MAX) return false;
            ++count;
        }

        sector.materials.assign(material_count, TerrainMaterialDescriptor{});
        std::vector<std::uint16_t> cursors(counts.size(), 0u);
        std::uint32_t tile_cursor = 0;
        for (std::uint32_t material = 0; material < material_count; ++material) {
            for (std::uint32_t part = 0; part < 4u; ++part) {
                const std::size_t slot = static_cast<std::size_t>(material) * 4u + part;
                auto& range = sector.materials[material].subparts[part];
                range.tile_begin = static_cast<std::uint16_t>(tile_cursor);
                range.tile_count = counts[slot];
                cursors[slot] = range.tile_begin;
                tile_cursor += range.tile_count;
            }
        }
        if (tile_cursor != sector.tiles.size()) return false;

        std::vector<TerrainTileDescriptor> ordered(sector.tiles.size());
        for (auto& tile : sector.tiles) {
            const std::size_t slot = static_cast<std::size_t>(tile.material_index) *
                4u + tile.subpart;
            ordered[cursors[slot]++] = std::move(tile);
        }
        sector.tiles = std::move(ordered);

        for (auto& material : sector.materials) {
            for (auto& range : material.subparts) {
                const std::uint32_t end =
                    static_cast<std::uint32_t>(range.tile_begin) + range.tile_count;
                for (std::uint32_t lod = 0; lod < 3u; ++lod) {
                    for (std::uint32_t stitch = 0; stitch < 4u; ++stitch) {
                        std::uint32_t count = 0;
                        for (std::uint32_t tile = range.tile_begin; tile < end; ++tile)
                            count += terrain_descriptor_index_count(
                                static_cast<std::uint8_t>(lod),
                                sector.tiles[tile].edge_mask, stitch);
                        if (count > UINT16_MAX) return false;
                        range.index_counts[lod][stitch] =
                            static_cast<std::uint16_t>(count);
                    }
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

static bool build_terrain_render_envelopes(TerrainCpuAsset& source) noexcept {
    try {
        for (auto& sector : source.sectors) for (auto& material : sector.materials)
            for (auto& range : material.subparts) {
                range.envelopes.clear();
                range.liquid_depth_objects.clear();
                range.required_animation_phases=0;
                const auto end = std::uint32_t(range.tile_begin) + range.tile_count;
                for (std::uint32_t tile = range.tile_begin; tile < end; ++tile) {
                    if (sector.tiles[tile].animation_ordinal!=UINT_MAX)
                        range.required_animation_phases=(std::max)(range.required_animation_phases,
                            sector.tiles[tile].animation_ordinal+1);
                    for (auto index : sector.tiles[tile].vertex_indices) {
                        const auto liquid = terrain_base_liquid_object(source,sector,index);
                        if (liquid != UINT16_MAX && std::find(range.liquid_depth_objects.begin(),
                            range.liquid_depth_objects.end(),liquid)==range.liquid_depth_objects.end())
                            range.liquid_depth_objects.push_back(liquid);
                        const auto& vertex = sector.vertices[index];
                        // Layer vertices wave and shift with their shared owner object.
                        const auto object = index >= kTerrainVerticesPerSector
                            ? owned_terrain_layer_object(source,sector,index-kTerrainVerticesPerSector) : UINT16_MAX;
                        if (object != UINT16_MAX && object >= source.objects.size()) return false;
                        const float wave = object == UINT16_MAX ? 0 :
                            std::fabs(source.objects[object].wave_multiplier);
                        const std::array<float,3> point{vertex.x,vertex.y,vertex.z};
                        for (float value : point) if (!std::isfinite(value)) return false;
                        if (!std::isfinite(wave)) return false;
                        auto found = std::find_if(range.envelopes.begin(),range.envelopes.end(),
                            [object](const auto& value) { return value.layer_object == object; });
                        if (found == range.envelopes.end()) {
                            TerrainTileRangeDescriptor::Envelope value{};
                            value.layer_object = static_cast<std::uint16_t>(object);
                            value.bounds = {point,point};
                            value.maximum_wave_multiplier = wave;
                            range.envelopes.push_back(value);
                        } else {
                            for (unsigned axis=0;axis<3;++axis) {
                                found->bounds.minimum[axis]=(std::min)(found->bounds.minimum[axis],point[axis]);
                                found->bounds.maximum[axis]=(std::max)(found->bounds.maximum[axis],point[axis]);
                            }
                            found->maximum_wave_multiplier=(std::max)(found->maximum_wave_multiplier,wave);
                        }
                    }
                }
            }
        return true;
    } catch (...) { return false; }
}

// Shipped maps drop their outer two vertex rows to height 0, liquid layer
// included (Dejavu zone7: water at 5.4 m down to 0 at rows 0-1), so a liquid
// reaching a map edge ends in an authored 5 m sheet that the renderer shaded
// as a waterfall (current, cascade foam, curtain). Render only: each liquid
// layer vertex in that rim takes the height of the first vertex inside it
// when the rim lies more than a fall step (kLiquidFallEdgeDrop) below it, so
// the map edge never reads as a fall. Shared edges with matching heights are
// untouched. The game's own liquid data is not changed.
static void flatten_liquid_rim(TerrainCpuAsset& source) noexcept {
    constexpr int rim = 2;
    const int xmax = int(source.sector_count_x * 32), ymax = int(source.sector_count_y * 32);
    if (xmax <= 2 * rim || ymax <= 2 * rim) return;
    for (auto& sector : source.sectors) {
        if (!sector.has_layers || sector.vertices.size() < 2u * kTerrainVerticesPerSector) continue;
        for (int row = 0; row < 33; ++row) for (int col = 0; col < 33; ++col) {
            const int gx = int(sector.map_x) * 32 + col, gy = int(sector.map_y) * 32 + row;
            if (gx >= rim && gy >= rim && gx <= xmax - rim && gy <= ymax - rim) continue;
            const unsigned index = unsigned(row * 33 + col);
            if (terrain_base_liquid_object(source, sector, index) == UINT16_MAX) continue;
            // The first vertex inside the rim, in the same sector (the rim is
            // the outer two rows of a 33-row sector).
            const int ix = (std::clamp)(gx, rim, xmax - rim) - int(sector.map_x) * 32;
            const int iy = (std::clamp)(gy, rim, ymax - rim) - int(sector.map_y) * 32;
            if (ix < 0 || iy < 0 || ix > 32 || iy > 32) continue;
            const unsigned inner = unsigned(iy * 33 + ix);
            if (terrain_base_liquid_object(source, sector, inner) == UINT16_MAX) continue;
            auto& layer = sector.vertices[kTerrainVerticesPerSector + index];
            const float target = sector.vertices[kTerrainVerticesPerSector + inner].z;
            if (std::isfinite(target) && std::isfinite(layer.z) && layer.z < target - kLiquidFallEdgeDrop)
                layer.z = target;
        }
    }
}

bool validate_terrain_asset_descriptor_once(
    TerrainCpuAsset& source) noexcept {
    if (source.immutable_descriptor_validation_count != 0u)
        return source.immutable_descriptor_validation_count == 1u &&
            source.immutable_descriptor_validated;
    source.immutable_descriptor_validation_count = 1u;
    source.immutable_descriptor_validated = false;

    const std::uint64_t sector_count =
        static_cast<std::uint64_t>(source.sector_count_x) *
        source.sector_count_y;
    if (!source.sector_count_x || !source.sector_count_y ||
        sector_count != source.sectors.size() || !source.texture_count ||
        source.texture_count > 32766u ||
        source.texture_resources.size() !=
            static_cast<std::size_t>(source.texture_count) +
                kTerrainLongTextureCount ||
        !source.tiles_per_texture_side || !source.tiles_per_texture ||
        source.tiles_per_texture !=
            source.tiles_per_texture_side * source.tiles_per_texture_side ||
        source.tile_types.size() !=
            static_cast<std::size_t>(source.tile_count) +
                kTerrainLongTextureCount * kTerrainTilesPerLongTexture ||
        source.tile_animation_phases.size() != source.tile_types.size())
        return false;

    const std::uint32_t material_count =
        (source.texture_count + kTerrainLongTextureCount) * 2u;
    const auto valid_bounds = [](const TerrainBounds& bounds) noexcept {
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(bounds.minimum[axis]) ||
                !std::isfinite(bounds.maximum[axis]) ||
                bounds.minimum[axis] > bounds.maximum[axis])
                return false;
        return true;
    };
    for (std::size_t index = 0; index < source.sectors.size(); ++index) {
        const auto& sector = source.sectors[index];
        if (sector.map_x != index % source.sector_count_x ||
            sector.map_y != index / source.sector_count_x ||
            !std::isfinite(sector.average_z) ||
            !valid_bounds(sector.bounds) ||
            sector.vertices.size() != kTerrainVerticesPerSector *
                (sector.has_layers ? 2u : 1u) ||
            sector.materials.size() != material_count)
            return false;
        for (const auto& bounds : sector.subpart_bounds)
            if (!valid_bounds(bounds)) return false;

        std::array<bool, kTerrainTilesPerSector * 2u> animation_seen{};
        if (sector.animation_count > animation_seen.size()) return false;
        std::uint32_t expected_begin = 0;
        for (std::uint32_t material = 0; material < material_count; ++material) {
            for (std::uint32_t part = 0; part < 4u; ++part) {
                const auto& range = sector.materials[material].subparts[part];
                const std::uint32_t end =
                    static_cast<std::uint32_t>(range.tile_begin) + range.tile_count;
                if (range.tile_begin != expected_begin || end > sector.tiles.size())
                    return false;
                for (std::uint32_t tile_index = range.tile_begin;
                     tile_index < end; ++tile_index) {
                    const auto& tile = sector.tiles[tile_index];
                    if (tile.material_index != material || tile.subpart != part ||
                        tile.tile_index >= source.tile_types.size() ||
                        tile.rotation >= 4u || tile.edge_mask >= 16u ||
                        tile.transparent !=
                            (tile.material_index >= source.texture_count +
                                kTerrainLongTextureCount) ||
                        tile.animation_phases !=
                            source.tile_animation_phases[tile.tile_index] ||
                        (tile.animation_ordinal == UINT_MAX) !=
                            (tile.animation_phases == 0u))
                        return false;
                    if (tile.animation_ordinal != UINT_MAX) {
                        if (tile.animation_ordinal >= sector.animation_count ||
                            animation_seen[tile.animation_ordinal])
                            return false;
                        animation_seen[tile.animation_ordinal] = true;
                    }
                    for (const auto vertex : tile.vertex_indices)
                        if (vertex >= sector.vertices.size()) return false;
                }
                for (std::uint32_t lod = 0; lod < 3u; ++lod)
                    for (std::uint32_t stitch = 0; stitch < 4u; ++stitch) {
                        std::uint32_t count = 0;
                        for (std::uint32_t tile_index = range.tile_begin;
                             tile_index < end; ++tile_index)
                            count += terrain_descriptor_index_count(
                                static_cast<std::uint8_t>(lod),
                                sector.tiles[tile_index].edge_mask, stitch);
                        if (range.index_counts[lod][stitch] != count) return false;
                    }
                expected_begin = end;
            }
        }
        if (expected_begin != sector.tiles.size()) return false;
        for (std::uint32_t ordinal = 0; ordinal < sector.animation_count; ++ordinal)
            if (!animation_seen[ordinal]) return false;
    }
    flatten_liquid_rim(source);
    owned_terrain_reset_liquid_outflow(source); // level river reaches, built on first use
    if (!build_terrain_render_envelopes(source)) return false;
    // Procedural falls: derived here, under the loading screen, before the
    // asset is shared (their sheets widen their host ranges' envelopes).
    try { attach_procedural_falls(source); } catch (...) { return false; }
    source.immutable_descriptor_validated = true;
    return true;
}

bool parse_terrain_asset(const ResourceBlob& blob,
                         const std::string& project_resource,
                         TerrainCpuAsset& terrain,
                         ResourceFailure& failure) {
    terrain = {};
    if (!blob.bytes || project_resource.empty()) {
        fail(failure, ResourceFailureCode::parse,
             "MPR terrain source is empty");
        return false;
    }
    MemoryResourceArchive archive;
    if (!archive.open(blob, failure)) return false;
    // The game lists "*.mp" in archive table order and uses the first match;
    // its single-header assertion is not in the shipped build. Do the same.
    std::string header_name;
    std::uint32_t header_candidates = 0;
    for (const std::string& name : archive.entry_names()) {
        if (name.size() < 3 || name.compare(name.size() - 3, 3, ".mp") != 0)
            continue;
        if (header_name.empty()) header_name = name;
        ++header_candidates;
    }
    if (header_candidates > 1) {
        char detail[256];
        std::snprintf(detail, sizeof(detail), "%s: %u map headers, using %s (game order)",
                      blob.identity.canonical_name.c_str(), header_candidates,
                      header_name.c_str());
        bridge_append_probe_event("terrain_extra_map_headers", detail);
    }
    if (header_name.empty()) {
        fail(failure, ResourceFailureCode::parse,
             "MPR contains no map header");
        return false;
    }
    return parse_terrain_members(blob.identity, project_resource, header_name,
        [&archive](const std::string& name, ResourceBlob& member, ResourceFailure& error) {
            return archive.resolve(name, member, error);
        }, terrain, failure);
}

bool parse_terrain_members(const ResourceIdentity& identity, const std::string& project_resource,
    const std::string& header_name, const TerrainMemberResolver& resolve,
    TerrainCpuAsset& terrain, ResourceFailure& failure) {
    terrain = {};
    if (!resolve || project_resource.empty() || header_name.size() < 3 ||
        header_name.compare(header_name.size() - 3, 3, ".mp") != 0) {
        fail(failure, ResourceFailureCode::parse, "Terrain member source is invalid");
        return false;
    }
    ResourceBlob header;
    if (!resolve(header_name, header, failure) || !header.bytes ||
        header.bytes->size() < kMapHeaderBytes) {
        if (!failure)
            fail(failure, ResourceFailureCode::parse,
                 "MPR map header is truncated");
        return false;
    }
    const auto& bytes = *header.bytes;
    // The game scales heights by max_z without checking it. Zero or negative
    // values are drawn as the game draws them; only a non-finite value is
    // made GPU-safe (flat at zero) and logged.
    float maximum_z = read_float(bytes.data() + 4u);
    if (!std::isfinite(maximum_z)) {
        bridge_append_probe_event("terrain_max_z_repaired",
            (header.identity.canonical_name + ": non-finite max_z drawn flat").c_str());
        maximum_z = 0.0f;
    }
    const std::uint32_t sectors_x = read_u32(bytes.data() + 8u);
    const std::uint32_t sectors_y = read_u32(bytes.data() + 12u);
    const std::uint32_t textures = read_u32(bytes.data() + 16u);
    const std::uint32_t texture_size = read_u32(bytes.data() + 20u);
    const std::uint32_t tiles = read_u32(bytes.data() + 24u);
    const std::uint32_t tile_size = read_u32(bytes.data() + 28u);
    const std::uint16_t objects = read_u16(bytes.data() + 32u);
    const std::uint32_t animated_tiles = read_u32(bytes.data() + 34u);
    std::uint64_t expected_header = kMapHeaderBytes;
    std::uint64_t part = 0;
    // texture_size % tile_size is only a debug assertion in the game, which
    // truncates the tiles-per-side division; a zero quotient divides by zero.
    if (read_u32(bytes.data()) != kMapSignature ||
        !sectors_x || !sectors_y ||
        !textures || textures >
            (static_cast<std::uint32_t>(UINT16_MAX) + 1u) / 2u -
                kTerrainLongTextureCount ||
        !texture_size || !tile_size ||
        texture_size / tile_size == 0 || texture_size / tile_size > 4096u ||
        !tiles ||
        sectors_x > static_cast<std::uint32_t>(
            (std::numeric_limits<int>::max)()) / kTerrainCellsPerSector ||
        sectors_y > static_cast<std::uint32_t>(
            (std::numeric_limits<int>::max)()) / kTerrainCellsPerSector ||
        !checked_multiply(objects, kMapObjectBytes, part) ||
        !checked_add(expected_header, part, expected_header) ||
        !checked_multiply(tiles, sizeof(std::uint32_t), part) ||
        !checked_add(expected_header, part, expected_header) ||
        !checked_multiply(animated_tiles, 2u * sizeof(std::uint16_t), part) ||
        !checked_add(expected_header, part, expected_header) ||
        expected_header != bytes.size()) {
        fail(failure, ResourceFailureCode::parse,
             "MPR map header contract is invalid");
        return false;
    }
    std::uint64_t sector_count = 0;
    std::uint64_t descriptor_count = 0;
    std::uint64_t descriptor_bytes = 0;
    const std::uint64_t material_count =
        (static_cast<std::uint64_t>(textures) + kTerrainLongTextureCount) * 2u;
    if (!checked_multiply(sectors_x, sectors_y, sector_count) ||
        sector_count > (std::numeric_limits<std::size_t>::max)() ||
        !checked_multiply(sector_count, material_count, descriptor_count) ||
        !checked_multiply(descriptor_count,
                          sizeof(TerrainMaterialDescriptor), descriptor_bytes) ||
        descriptor_bytes > (std::numeric_limits<std::size_t>::max)()) {
        fail(failure, ResourceFailureCode::parse,
             "MPR terrain descriptor dimensions overflow owned storage");
        return false;
    }
    terrain.identity = identity;
    terrain.project_resource = project_resource;
    terrain.map_header_resource = header_name;
    terrain.maximum_z = maximum_z;
    terrain.sector_count_x = sectors_x;
    terrain.sector_count_y = sectors_y;
    terrain.texture_count = textures;
    terrain.texture_size = texture_size;
    terrain.tile_count = tiles;
    terrain.tile_size = tile_size;
    terrain.tiles_per_texture_side = texture_size / tile_size;
    if (!checked_multiply(terrain.tiles_per_texture_side,
                          terrain.tiles_per_texture_side, part) ||
        part > UINT32_MAX) {
        fail(failure, ResourceFailureCode::parse,
             "MPR tile grid overflows owned material storage");
        return false;
    }
    terrain.tiles_per_texture = static_cast<std::uint32_t>(part);
    const std::string base = header_name.substr(0, header_name.size() - 3u);
    try {
        terrain.texture_resources.reserve(
            static_cast<std::size_t>(textures) + kTerrainLongTextureCount);
        for (std::uint32_t index = 0; index < textures; ++index) {
            char suffix[8]{};
            std::snprintf(suffix, sizeof(suffix), "%03u", index);
            terrain.texture_resources.push_back(base + suffix);
        }
        for (std::uint32_t index = 0; index < kTerrainLongTextureCount; ++index) {
            char suffix[8]{};
            std::snprintf(suffix, sizeof(suffix), "L%02u", index);
            terrain.texture_resources.push_back(base + suffix);
        }
        terrain.objects.resize(objects);
        const std::uint8_t* object_bytes = bytes.data() + kMapHeaderBytes;
        for (std::uint32_t index = 0; index < objects; ++index) {
            const auto* input = object_bytes +
                static_cast<std::size_t>(index) * kMapObjectBytes;
            auto& output = terrain.objects[index];
            output.type = read_u32(input);
            output.color = {read_float(input + 4u), read_float(input + 8u),
                            read_float(input + 12u)};
            output.opacity = read_float(input + 16u);
            output.self_illumination = read_float(input + 20u);
            output.wave_multiplier = read_float(input + 24u);
            output.warp_speed = read_float(input + 28u);
            // The game copies the record unchecked: a type beyond 4 is only
            // "not grass, not water", and its floats are used as stored.
            // Keep both; only a non-finite float is made GPU-safe (zero).
            bool repaired = false;
            for (float* value : {&output.color[0], &output.color[1], &output.color[2],
                                 &output.opacity, &output.self_illumination,
                                 &output.wave_multiplier, &output.warp_speed})
                if (!std::isfinite(*value)) { *value = 0.0f; repaired = true; }
            if (repaired) {
                char detail[256];
                std::snprintf(detail, sizeof(detail), "%s: object %u non-finite fields set to 0",
                              header.identity.canonical_name.c_str(), index);
                bridge_append_probe_event("terrain_object_repaired", detail);
            }
        }
        const std::uint8_t* tile_types = object_bytes +
            static_cast<std::size_t>(objects) * kMapObjectBytes;
        terrain.tile_types.resize(tiles +
            kTerrainLongTextureCount * kTerrainTilesPerLongTexture, 3u);
        terrain.tile_animation_phases.resize(terrain.tile_types.size(), 0u);
        // The supported loader copies the declared DWORD table verbatim; it
        // does not narrow this gameplay terrain classification to an enum.
        // Shipped maps use combined value 6, so all 32-bit values are retained.
        for (std::uint32_t index = 0; index < tiles; ++index) {
            terrain.tile_types[index] = read_u32(tile_types + index * 4u);
        }
        const std::uint8_t* animations = tile_types +
            static_cast<std::size_t>(tiles) * 4u;
        for (std::uint32_t index = 0; index < animated_tiles; ++index) {
            const std::uint16_t tile_index = read_u16(animations + index * 4u);
            const std::uint16_t phases = read_u16(animations + index * 4u + 2u);
            // The game stores phases[index] = count: a repeated index keeps the
            // last count and zero means "not animated". An index or phase run
            // past the tile table is an out-of-bounds access in the game.
            if (tile_index >= tiles ||
                static_cast<std::uint32_t>(tile_index) + phases > tiles) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR animated tile descriptor is invalid");
                return false;
            }
            terrain.tile_animation_phases[tile_index] = phases;
        }
    } catch (...) {
        fail(failure, ResourceFailureCode::parse,
             "MPR immutable descriptor allocation failed");
        return false;
    }
    try { terrain.sectors.resize(static_cast<std::size_t>(sector_count)); }
    catch (...) {
        fail(failure, ResourceFailureCode::parse,
             "MPR sector allocation failed");
        return false;
    }
    for (std::uint32_t sy = 0; sy < sectors_y; ++sy) {
        for (std::uint32_t sx = 0; sx < sectors_x; ++sx) {
            char suffix[16]{};
            std::snprintf(suffix, sizeof(suffix), "%03u%03u.sec", sx, sy);
            ResourceBlob sector_blob;
            const std::string sector_name = base + suffix;
            if (!resolve(sector_name, sector_blob, failure) ||
                !sector_blob.bytes || sector_blob.bytes->size() < 5u)
                return false;
            const auto& sector_bytes = *sector_blob.bytes;
            // The game reads layers = flags & 1 and layer tiles = flags & 3,
            // so bit 0 alone also carries layer tiles, and bits 2-7 are
            // ignored. Layer tiles without layers (flags & 3 == 2) leave the
            // game's object-id array empty and crash it on first draw.
            const std::uint8_t flags = sector_bytes[4] & 0x03u;
            if (read_u32(sector_bytes.data()) != kSectorSignatureNormals ||
                flags == 0x02u) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR sector signature or flags are unsupported");
                return false;
            }
            std::uint64_t expected_sector = 5u +
                static_cast<std::uint64_t>(kTerrainVerticesPerSector) *
                    kTerrainVertexBytes + kTerrainTileBytes;
            if (flags & 0x01u)
                expected_sector +=
                    static_cast<std::uint64_t>(kTerrainVerticesPerSector) *
                        kTerrainVertexBytes + kTerrainTileBytes;
            if (flags != 0u) expected_sector += kTerrainTileBytes;
            if (expected_sector != sector_bytes.size()) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR sector byte count does not match its flags");
                return false;
            }
            TerrainSectorCpuAsset& output = terrain.sectors[
                static_cast<std::size_t>(sy) * sectors_x + sx];
            output.identity = sector_blob.identity;
            output.map_x = sx;
            output.map_y = sy;
            output.has_layers = (flags & 0x01u) != 0;
            output.has_layer_tiles = flags != 0u;
            const std::size_t vertex_count = kTerrainVerticesPerSector *
                (output.has_layers ? 2u : 1u);
            try { output.vertices.resize(vertex_count); }
            catch (...) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR terrain vertex allocation failed");
                return false;
            }
            const std::uint8_t* input = sector_bytes.data() + 5u;
            const std::size_t base_vertex_bytes =
                static_cast<std::size_t>(kTerrainVerticesPerSector) *
                kTerrainVertexBytes;
            const std::uint8_t* layer_input = output.has_layers
                ? input + base_vertex_bytes : nullptr;
            const std::uint8_t* base_tiles = input + base_vertex_bytes *
                (output.has_layers ? 2u : 1u);
            const std::uint8_t* layer_tiles = output.has_layer_tiles
                ? base_tiles + kTerrainTileBytes : nullptr;
            const std::uint8_t* object_ids = output.has_layers
                ? base_tiles + kTerrainTileBytes +
                    (output.has_layer_tiles ? kTerrainTileBytes : 0u)
                : nullptr;
            auto object_id_at = [&](std::uint32_t index) noexcept {
                return object_ids ? read_u16(object_ids + index * 2u) :
                    kTerrainUndefined;
            };
            for (std::uint32_t y = 0; y < kTerrainVerticesPerSide; ++y) {
                for (std::uint32_t x = 0; x < kTerrainVerticesPerSide; ++x) {
                    const std::size_t index =
                        static_cast<std::size_t>(y) * kTerrainVerticesPerSide + x;
                    const std::uint8_t* encoded = input + index * kTerrainVertexBytes;
                    TerrainVertex& vertex = output.vertices[index];
                    vertex.x = static_cast<float>(sx * kTerrainCellsPerSector + x) +
                        static_cast<float>(static_cast<std::int8_t>(encoded[0])) *
                            (0.5f / 126.0f);
                    vertex.y = static_cast<float>(sy * kTerrainCellsPerSector + y) +
                        static_cast<float>(static_cast<std::int8_t>(encoded[1])) *
                            (0.5f / 126.0f);
                    vertex.z = static_cast<float>(read_u16(encoded + 2u)) *
                        maximum_z / 65534.0f;
                    decode_terrain_normal(read_u32(encoded + 4u), vertex);
                }
            }
            if (output.has_layers) {
                for (std::uint32_t y = 0; y < kTerrainVerticesPerSide; ++y) {
                    for (std::uint32_t x = 0; x < kTerrainVerticesPerSide; ++x) {
                        const std::size_t grid =
                            static_cast<std::size_t>(y) * kTerrainVerticesPerSide + x;
                        const std::uint8_t* encoded =
                            layer_input + grid * kTerrainVertexBytes;
                        TerrainVertex& vertex =
                            output.vertices[kTerrainVerticesPerSector + grid];
                        std::uint32_t adjacent_count = 0;
                        const auto adjacent = terrain_adjacent_tiles(x, y, adjacent_count);
                        std::uint16_t geometry_object = kTerrainUndefined;
                        std::uint16_t lighting_object = kTerrainUndefined;
                        for (std::uint32_t item = 0; item < adjacent_count; ++item) {
                            const std::uint16_t candidate = object_id_at(adjacent[item]);
                            if (candidate == kTerrainUndefined) continue;
                            if (candidate >= terrain.objects.size()) {
                                fail(failure, ResourceFailureCode::parse,
                                     "MPR layer object selector is out of range");
                                return false;
                            }
                            if (lighting_object == kTerrainUndefined)
                                lighting_object = candidate;
                            if (geometry_object == kTerrainUndefined ||
                                terrain.objects[candidate].wave_multiplier <
                                    terrain.objects[geometry_object].wave_multiplier)
                                geometry_object = candidate;
                        }
                        vertex.geometry_object = geometry_object;
                        vertex.lighting_object = lighting_object;
                        vertex.x = static_cast<float>(
                            sx * kTerrainCellsPerSector + x);
                        vertex.y = static_cast<float>(
                            sy * kTerrainCellsPerSector + y);
                        if (geometry_object != kTerrainUndefined &&
                            terrain.objects[geometry_object].type == 4u) {
                            vertex.x = output.vertices[grid].x;
                            vertex.y = output.vertices[grid].y;
                        }
                        vertex.z = geometry_object == kTerrainUndefined ? 0.0f :
                            static_cast<float>(read_u16(encoded + 2u)) *
                                maximum_z / 65534.0f;
                        vertex.depth = vertex.z - output.vertices[grid].z;
                        decode_terrain_normal(read_u32(encoded + 4u), vertex);
                    }
                }
            }
            if (!finalize_terrain_sector_spatial_descriptor(output)) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR terrain clip bounds are invalid");
                return false;
            }

            std::uint32_t animation_ordinal = 0;
            auto append_tile = [&](std::uint32_t tile_x, std::uint32_t tile_y,
                                   const std::uint8_t* ids, bool layer,
                                   std::uint16_t object_id) -> bool {
                const std::uint32_t tile_offset = tile_y * kTerrainTilesPerSide + tile_x;
                // Layer membership comes from the object selector. The shipped
                // loader skips absent objects before interpreting their tile
                // word, which may retain an unused value in production maps.
                if (layer && object_id == kTerrainUndefined) return true;
                const std::uint16_t encoded = read_u16(ids + tile_offset * 2u);
                if (encoded == kTerrainUndefined) return false;
                const std::uint32_t tile_index = encoded & 0x3fffu;
                if (tile_index >= terrain.tile_types.size()) return false;
                std::uint16_t material = 0;
                std::uint32_t tile_position = 0;
                if (!terrain_material(tile_index, terrain, material, tile_position))
                    return false;
                TerrainTileDescriptor tile{};
                tile.tile_index = static_cast<std::uint16_t>(tile_index);
                tile.material_index = static_cast<std::uint16_t>(material +
                    (layer ? textures + kTerrainLongTextureCount : 0u));
                tile.object_id = layer ? object_id : kTerrainUndefined;
                tile.rotation = static_cast<std::uint8_t>(encoded >> 14u);
                tile.subpart = static_cast<std::uint8_t>(
                    2u * ((2u * tile_y) / kTerrainTilesPerSide) +
                    (2u * tile_x) / kTerrainTilesPerSide);
                tile.edge_mask = static_cast<std::uint8_t>(
                    (tile_y == 0u ? 1u : 0u) |
                    (tile_y + 1u == kTerrainTilesPerSide ? 2u : 0u) |
                    (tile_x == 0u ? 4u : 0u) |
                    (tile_x + 1u == kTerrainTilesPerSide ? 8u : 0u));
                tile.transparent = layer;
                if (!layer) output.base_tile_ids[tile_offset] = tile.tile_index;
                tile.animation_phases = terrain.tile_animation_phases[tile_index];
                if (tile.animation_phases)
                    tile.animation_ordinal = animation_ordinal++;
                const std::uint32_t vertex_base = layer
                    ? kTerrainVerticesPerSector : 0u;
                for (std::uint32_t y = 0; y < 3u; ++y)
                    for (std::uint32_t x = 0; x < 3u; ++x)
                        tile.vertex_indices[y * 3u + x] =
                            static_cast<std::uint16_t>(vertex_base +
                                (tile_y * 2u + y) * kTerrainVerticesPerSide +
                                tile_x * 2u + x);
                output.tiles.push_back(std::move(tile));
                return true;
            };
            try { output.tiles.reserve(kTerrainTilesPerSector *
                (output.has_layer_tiles ? 2u : 1u)); }
            catch (...) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR terrain tile allocation failed");
                return false;
            }
            for (std::uint32_t y = 0; y < kTerrainTilesPerSide; ++y)
                for (std::uint32_t x = 0; x < kTerrainTilesPerSide; ++x)
                    if (!append_tile(x, y, base_tiles, false, kTerrainUndefined)) {
                        fail(failure, ResourceFailureCode::parse,
                             "MPR base tile selector is invalid");
                        return false;
                    }
            if (output.has_layer_tiles)
                for (std::uint32_t y = 0; y < kTerrainTilesPerSide; ++y)
                    for (std::uint32_t x = 0; x < kTerrainTilesPerSide; ++x) {
                        const std::uint32_t tile_offset =
                            y * kTerrainTilesPerSide + x;
                        if (!append_tile(x, y, layer_tiles, true,
                                         object_id_at(tile_offset))) {
                            fail(failure, ResourceFailureCode::parse,
                                 "MPR layer tile selector is invalid");
                            return false;
                        }
                    }
            output.animation_count = animation_ordinal;
            if (!publish_terrain_sector_descriptors(
                    terrain, output, static_cast<std::uint32_t>(material_count))) {
                fail(failure, ResourceFailureCode::parse,
                     "MPR terrain command descriptor publication failed");
                return false;
            }
        }
    }
    if (!validate_terrain_asset_descriptor_once(terrain)) {
        fail(failure, ResourceFailureCode::parse,
             "MPR immutable terrain descriptor is invalid");
        return false;
    }
    failure = {};
    return true;
}

bool finalize_terrain_sector_spatial_descriptor(
    TerrainSectorCpuAsset& sector) noexcept {
    const std::size_t expected = static_cast<std::size_t>(
        kTerrainVerticesPerSector) * (sector.has_layers ? 2u : 1u);
    if (sector.vertices.size() != expected) return false;
    float minimum_z = (std::numeric_limits<float>::max)();
    float maximum_z = 0.0f;
    for (const auto& vertex : sector.vertices) {
        if (!std::isfinite(vertex.z)) return false;
        minimum_z = (std::min)(minimum_z, vertex.z);
        maximum_z = (std::max)(maximum_z, vertex.z);
    }
    // The runtime scans every allocated mesh vertex. An undefined upper-layer
    // point is decoded at the map minimum (zero), so it participates in these
    // extrema. The later +/- one-cell wave margin cancels out of AverageZ.
    sector.average_z = (minimum_z + maximum_z) * 0.5f;
    const float sector_x = static_cast<float>(
        sector.map_x * kTerrainCellsPerSector);
    const float sector_y = static_cast<float>(
        sector.map_y * kTerrainCellsPerSector);
    sector.bounds.minimum = {sector_x, sector_y, minimum_z - 1.0f};
    sector.bounds.maximum = {
        sector_x + static_cast<float>(kTerrainCellsPerSector),
        sector_y + static_cast<float>(kTerrainCellsPerSector),
        maximum_z + 1.0f};
    for (std::uint32_t part = 0; part < 4u; ++part) {
        const float x = sector_x + static_cast<float>(part & 1u) * 16.0f;
        const float y = sector_y + static_cast<float>(part >> 1u) * 16.0f;
        sector.subpart_bounds[part].minimum = {x, y, minimum_z - 1.0f};
        sector.subpart_bounds[part].maximum = {x + 16.0f, y + 16.0f,
                                               maximum_z + 1.0f};
    }
    return true;
}

namespace {

// The height fields of one vertex: from the full record, or from a core-mode
// slim sector's compact copy of the same values (TerrainSectorCpuAsset::heights).
const TerrainHeightVertex* terrain_vertex(const TerrainCpuAsset& terrain,
                                          int x, int y, bool layer,
                                          TerrainHeightVertex& storage) noexcept {
    if (x < 0 || y < 0 ||
        x > static_cast<int>(terrain.sector_count_x * kTerrainCellsPerSector) ||
        y > static_cast<int>(terrain.sector_count_y * kTerrainCellsPerSector))
        return nullptr;
    int sector_x = x / static_cast<int>(kTerrainCellsPerSector);
    int sector_y = y / static_cast<int>(kTerrainCellsPerSector);
    int local_x = x - sector_x * static_cast<int>(kTerrainCellsPerSector);
    int local_y = y - sector_y * static_cast<int>(kTerrainCellsPerSector);
    if (local_x == 0 && sector_x != 0) {
        --sector_x;
        local_x = static_cast<int>(kTerrainCellsPerSector);
    }
    if (local_y == 0 && sector_y != 0) {
        --sector_y;
        local_y = static_cast<int>(kTerrainCellsPerSector);
    }
    if (sector_x < 0 || sector_y < 0 ||
        sector_x >= static_cast<int>(terrain.sector_count_x) ||
        sector_y >= static_cast<int>(terrain.sector_count_y))
        return nullptr;
    const std::size_t sector_index =
        static_cast<std::size_t>(sector_y) * terrain.sector_count_x + sector_x;
    if (sector_index >= terrain.sectors.size()) return nullptr;
    const TerrainSectorCpuAsset& sector = terrain.sectors[sector_index];
    if (layer && !sector.has_layers) return nullptr;
    const std::size_t index = static_cast<std::size_t>(local_y) *
        kTerrainVerticesPerSide + static_cast<std::size_t>(local_x) +
        (layer && sector.has_layers ? kTerrainVerticesPerSector : 0u);
    if (!sector.vertices.empty()) {
        if (index >= sector.vertices.size()) return nullptr;
        storage = terrain_height_vertex(sector.vertices[index]);
        return &storage;
    }
    return index < sector.heights.size() ? &sector.heights[index] : nullptr;
}

float terrain_cross(const TerrainHeightVertex& center, const TerrainHeightVertex& first,
                    const TerrainHeightVertex& second) noexcept {
    return (first.x - center.x) * (second.y - center.y) -
        (first.y - center.y) * (second.x - center.x);
}

float terrain_plane_height(float x, float y, const TerrainHeightVertex& first,
                           const TerrainHeightVertex& second,
                           const TerrainHeightVertex& third) noexcept {
    const float e1 = second.x - first.x;
    const float e2 = second.y - first.y;
    const float e3 = second.z - first.z;
    const float e4 = third.x - first.x;
    const float e5 = third.y - first.y;
    const float e6 = third.z - first.z;
    const float c = e1 * e5 - e2 * e4;
    if (std::fabs(c) < 0.01f)
        return (first.z + second.z + third.z) / 3.0f;
    const float a = e2 * e6 - e3 * e5;
    const float b = e3 * e4 - e1 * e6;
    const float d = -(a * first.x + b * first.y + c * first.z);
    return -(a * x + b * y + d) / c;
}

} // namespace

static bool sample_terrain_layer(const TerrainCpuAsset& terrain, float x, float y,
                                 bool layer, float& height,
                                 const std::vector<float>* shifts = nullptr) noexcept {
    height = 0.0f;
    if (!std::isfinite(x) || !std::isfinite(y) || terrain.sectors.empty() ||
        x < 0.0f || y < 0.0f ||
        x > static_cast<float>(terrain.sector_count_x * kTerrainCellsPerSector) ||
        y > static_cast<float>(terrain.sector_count_y * kTerrainCellsPerSector))
        return false;
    int cell_x = static_cast<int>(std::nearbyint(x));
    int cell_y = static_cast<int>(std::nearbyint(y));
    for (unsigned attempt = 0; attempt < 10u; ++attempt) {
        std::array<TerrainHeightVertex,4> read{};
        const TerrainHeightVertex* lu = terrain_vertex(terrain, cell_x, cell_y, layer, read[0]);
        const TerrainHeightVertex* ru = terrain_vertex(terrain, cell_x + 1, cell_y, layer, read[1]);
        const TerrainHeightVertex* ld = terrain_vertex(terrain, cell_x, cell_y + 1, layer, read[2]);
        const TerrainHeightVertex* rd = terrain_vertex(terrain, cell_x + 1, cell_y + 1, layer, read[3]);
        std::array<TerrainHeightVertex,4> current{};
        const auto shifted=[&](const TerrainHeightVertex*& vertex, unsigned slot) noexcept {
            if (!vertex || !layer || !shifts) return true;
            current[slot]=*vertex;
            if (vertex->lighting_object!=UINT16_MAX) {
                if (vertex->lighting_object>=shifts->size()) return false;
                current[slot].z+=(*shifts)[vertex->lighting_object];
                if (!std::isfinite(current[slot].z)) return false;
            }
            vertex=&current[slot]; return true;
        };
        if (!shifted(lu,0) || !shifted(ru,1) || !shifted(ld,2) || !shifted(rd,3)) return false;
        if (!lu || !rd) {
            height = 0.0f;
            return true;
        }
        if (ru && !ld) {
            height = terrain_plane_height(x, y, *lu, *ru, *rd);
            return std::isfinite(height);
        }
        if (!ru && ld) {
            height = terrain_plane_height(x, y, *lu, *rd, *ld);
            return std::isfinite(height);
        }
        if (!ru || !ld) {
            height = 0.0f;
            return true;
        }
        if (terrain_cross(*ru, *rd, *lu) <= 0.0f ||
            terrain_cross(*ld, *lu, *rd) <= 0.0f) {
            height = (ld->z + rd->z) * 0.5f;
            return std::isfinite(height);
        }
        const float upper_dx = ru->x - x;
        const float upper_dy = ru->y - y;
        const float lower_dx = ld->x - x;
        const float lower_dy = ld->y - y;
        const float diagonal = (x - lu->x) * (rd->y - lu->y) -
            (y - lu->y) * (rd->x - lu->x);
        if (diagonal > 0.0f) {
            if (attempt + 1u < 10u &&
                upper_dx * (lu->y - ru->y) -
                    upper_dy * (lu->x - ru->x) > 0.0f) {
                --cell_y;
                continue;
            }
            if (attempt + 1u < 10u &&
                upper_dx * (rd->y - ru->y) -
                    upper_dy * (rd->x - ru->x) < 0.0f) {
                ++cell_x;
                continue;
            }
            height = terrain_plane_height(x, y, *lu, *ru, *rd);
        } else {
            if (attempt + 1u < 10u &&
                lower_dx * (lu->y - ld->y) -
                    lower_dy * (lu->x - ld->x) < 0.0f) {
                --cell_x;
                continue;
            }
            if (attempt + 1u < 10u &&
                lower_dx * (rd->y - ld->y) -
                    lower_dy * (rd->x - ld->x) > 0.0f) {
                ++cell_y;
                continue;
            }
            height = terrain_plane_height(x, y, *lu, *rd, *ld);
        }
        return std::isfinite(height);
    }
    return false;
}

bool parse_generated_texture_asset(
    const std::shared_ptr<const std::vector<std::uint8_t>>& bytes,
    TextureCpuAsset& texture, ResourceFailure& failure) {
    TextureCpuAsset parsed{};
    try {
        ResourceBlob value;
        value.bytes = bytes;
        if (parse_texture_asset_impl(value, parsed, failure, TextureValueSource::generated_memory)) {
            texture = std::move(parsed);
            return true;
        }
    } catch (...) {
        fail(failure, ResourceFailureCode::parse,
             "generated MMP owned output allocation failed");
    }
    texture = {};
    return false;
}

bool parse_generated_disk_texture_asset(const ResourceBlob& blob,
    TextureCpuAsset& texture, ResourceFailure& failure) {
    TextureCpuAsset parsed{};
    try {
        if (parse_texture_asset_impl(blob, parsed, failure, TextureValueSource::generated_disk)) {
            texture = std::move(parsed);
            return true;
        }
    } catch (...) {
        fail(failure, ResourceFailureCode::parse, "generated disk MMP allocation failed");
    }
    texture = {};
    return false;
}

bool sample_terrain_height(const TerrainCpuAsset& terrain, float x, float y,
                           float& height) noexcept {
    return sample_terrain_layer(terrain, x, y, false, height);
}

bool intersect_base_terrain_ray(const TerrainCpuAsset& terrain,
    const std::array<double, 3>& origin, const std::array<double, 3>& direction,
    std::array<double, 3>& hit) noexcept {
    hit = {};
    if (!terrain.immutable_descriptor_validated || !terrain.sector_count_x ||
        !terrain.sector_count_y) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return false;
    const int width = static_cast<int>(terrain.sector_count_x * kTerrainCellsPerSector);
    const int height = static_cast<int>(terrain.sector_count_y * kTerrainCellsPerSector);
    // Source XY is perturbed by at most 128 * .5 / 126 from its nominal grid
    // coordinate. The one-cell halo below covers every triangle crossing a
    // nominal cell boundary, including sector seams.
    constexpr double halo = 128.0 * .5 / 126.0;
    double first = 0, last = 1;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const double low = -halo;
        const double high = (axis ? height : width) + halo;
        if (std::abs(direction[axis]) < 1e-15) {
            if (origin[axis] < low || origin[axis] > high) return false;
        } else {
            double a = (low-origin[axis])/direction[axis];
            double b = (high-origin[axis])/direction[axis];
            if (a > b) std::swap(a,b);
            first = (std::max)(first,a);
            last = (std::min)(last,b);
            if (first > last) return false;
        }
    }
    const auto triangle_hit = [&](const TerrainVertex& a, const TerrainVertex& b,
                                  const TerrainVertex& c, double& nearest) {
        const std::array<double,3> e1{double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z};
        const std::array<double,3> e2{double(c.x)-a.x,double(c.y)-a.y,double(c.z)-a.z};
        const std::array<double,3> cross{
            direction[1]*e2[2]-direction[2]*e2[1],
            direction[2]*e2[0]-direction[0]*e2[2],
            direction[0]*e2[1]-direction[1]*e2[0]};
        const double determinant=e1[0]*cross[0]+e1[1]*cross[1]+e1[2]*cross[2];
        if (std::abs(determinant)<1e-12) return;
        const std::array<double,3> offset{origin[0]-a.x,origin[1]-a.y,origin[2]-a.z};
        const double u=(offset[0]*cross[0]+offset[1]*cross[1]+offset[2]*cross[2])/determinant;
        if (u < -1e-9 || u > 1+1e-9) return;
        const std::array<double,3> q{
            offset[1]*e1[2]-offset[2]*e1[1],
            offset[2]*e1[0]-offset[0]*e1[2],
            offset[0]*e1[1]-offset[1]*e1[0]};
        const double v=(direction[0]*q[0]+direction[1]*q[1]+direction[2]*q[2])/determinant;
        if (v < -1e-9 || u+v > 1+1e-9) return;
        const double t=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])/determinant;
        if (t >= first-1e-9 && t <= last+1e-9 && t >= 0 && t < nearest)
            nearest=t;
    };
    double nearest = INFINITY;
    const auto test_cell = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const unsigned sx = static_cast<unsigned>(x)/kTerrainCellsPerSector;
        const unsigned sy = static_cast<unsigned>(y)/kTerrainCellsPerSector;
        const auto& sector=terrain.sectors[static_cast<std::size_t>(sy)*terrain.sector_count_x+sx];
        const unsigned lx=static_cast<unsigned>(x)%kTerrainCellsPerSector;
        const unsigned ly=static_cast<unsigned>(y)%kTerrainCellsPerSector;
        const std::size_t a=static_cast<std::size_t>(ly)*kTerrainVerticesPerSide+lx;
        if (a+kTerrainVerticesPerSide+1 >= sector.vertices.size()) return;
        // The source base grid uses the LU--RD diagonal. Query both triangles
        // without backface culling: the quality anchor has no render winding.
        triangle_hit(sector.vertices[a],sector.vertices[a+1],
                     sector.vertices[a+kTerrainVerticesPerSide+1],nearest);
        triangle_hit(sector.vertices[a],sector.vertices[a+kTerrainVerticesPerSide+1],
                     sector.vertices[a+kTerrainVerticesPerSide],nearest);
    };
    double t=first;
    int x=static_cast<int>(std::floor(origin[0]+direction[0]*t));
    int y=static_cast<int>(std::floor(origin[1]+direction[1]*t));
    const int step_x=direction[0]>0?1:(direction[0]<0?-1:0);
    const int step_y=direction[1]>0?1:(direction[1]<0?-1:0);
    const double delta_x=step_x?1/std::abs(direction[0]):INFINITY;
    const double delta_y=step_y?1/std::abs(direction[1]):INFINITY;
    double next_x=step_x?(double(x+(step_x>0))-origin[0])/direction[0]:INFINITY;
    double next_y=step_y?(double(y+(step_y>0))-origin[1])/direction[1]:INFINITY;
    // DDA traverses at most the map width plus height, regardless of camera
    // distance. Neighbor tests cover the decoded vertex perturbation.
    const int steps=width+height+8;
    for (int step=0;step<steps && t<=last+1e-12;++step) {
        for (int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            test_cell(x+dx,y+dy);
        if (nearest <= (std::min)(next_x,next_y)+1e-12) break;
        if (next_x < next_y) { t=next_x;x+=step_x;next_x+=delta_x; }
        else if (next_y < next_x) { t=next_y;y+=step_y;next_y+=delta_y; }
        else { t=next_x;x+=step_x;y+=step_y;next_x+=delta_x;next_y+=delta_y; }
        if (!std::isfinite(t)) break;
    }
    if (!std::isfinite(nearest)) return false;
    for (unsigned axis=0;axis<3;++axis) hit[axis]=origin[axis]+nearest*direction[axis];
    return true;
}

bool sample_terrain_height(const TerrainCpuAsset& terrain, float x, float y,
                           TerrainHeightLayer layer, float& height,
                           const std::vector<float>* shifts) noexcept {
    if (!sample_terrain_layer(terrain, x, y, false, height)) return false;
    if (layer == TerrainHeightLayer::ground) return true;
    float surface = 0.0f;
    if (!sample_terrain_layer(terrain, x, y, true, surface,shifts)) return false;
    height = (std::max)(height, surface);
    return true;
}

bool decode_model_asset(const ResourceStore& store,
                        const std::string& visual_reference,
                        ModelCpuAsset& model, ResourceFailure& failure) {
    model = {};
    ResourceLocation animated;
    ResourceFailure optional{};
    if (store.locate(visual_reference + ".anm", animated, optional)) {
        fail(failure, ResourceFailureCode::parse,
             "model has an animation archive and is outside the static slice");
        return false;
    }
    if (optional.code != ResourceFailureCode::not_found) {
        failure = optional;
        return false;
    }
    ResourceBlob mod_blob;
    ResourceBlob bone_blob;
    if (!store.resolve(visual_reference + ".mod", mod_blob, failure) ||
        !store.resolve(visual_reference + ".bon", bone_blob, failure))
        return false;
    return decode_model_resources(mod_blob, bone_blob, visual_reference,
                                  model, failure);
}

bool decode_model_resources(const ResourceBlob& mod_blob,
                            const ResourceBlob& bone_blob,
                            const std::string& visual_reference,
                            ModelCpuAsset& model, ResourceFailure& failure) {
    model = {};
    MemoryResourceArchive mod_archive, bone_archive;
    // BON endpoints describe the skeleton, independently of FIG shape count.
    // Share the active-map LNK/BON decoder, then attach each node's geometry.
    if (!mod_archive.open(mod_blob, failure) ||
        !bone_archive.open(bone_blob, failure))
        return false;
    ResourceBlob lnk_blob;
    if (!mod_archive.resolve(visual_reference, lnk_blob, failure)) return false;
    // BON endpoints describe the skeleton, independently of FIG shape count.
    // Use the same LNK/BON decoder as the active map's selected sources.
    if (!decode_selected_skeleton(lnk_blob, visual_reference,
        [&](const std::string& name, ResourceBlob& selected, ResourceFailure& error) {
            return bone_archive.resolve(name, selected, error);
        }, model, failure)) return false;
    for (ModelNodeCpuAsset& node : model.nodes) {
        ResourceBlob fig_blob;
        if (!mod_archive.resolve(node.name, fig_blob, failure) ||
            !parse_figure_asset(fig_blob, node.figure, failure)) {
            failure.message = node.name + ": " + failure.message;
            return false;
        }
        model.static_shape_parameterized |= node.figure.shape_count == 8;
    }
    model.model_container = mod_blob.identity;
    model.bone_container = bone_blob.identity;
    model.source_bytes = mod_blob.identity.bytes + bone_blob.identity.bytes;
    failure = {};
    return true;
}

bool parse_material_asset(const ResourceBlob& blob, MaterialCpuAsset& material,
                          ResourceFailure& failure) {
    material = {};
    if (!blob.bytes || blob.bytes->size() < 2) {
        fail(failure, ResourceFailureCode::archive_bounds, "MAT name length missing");
        return false;
    }
    const auto offset = static_cast<std::size_t>(read_u16(blob.bytes->data()))+2;
    if (offset > blob.bytes->size() || blob.bytes->size()-offset < 17*4) {
        fail(failure, ResourceFailureCode::archive_bounds, "MAT channels are truncated");
        return false;
    }
    MaterialCpuAsset result;
    result.identity = blob.identity;
    std::size_t cursor = offset;
    for (auto* channels : {&result.diffuse, &result.ambient, &result.specular, &result.emissive})
        for (auto& value : *channels) {
            value = read_float(blob.bytes->data()+cursor);
            cursor += 4;
            if (!std::isfinite(value)) value = 0.0f;  // unchecked by the game
        }
    result.power = read_float(blob.bytes->data()+cursor);
    if (!std::isfinite(result.power)) result.power = 0.0f;
    material = std::move(result);
    failure = {};
    return true;
}

bool decode_selected_skeleton(const ResourceBlob& links,
    const std::string& visual_reference, const SelectedBoneResolver& select_bone,
    ModelCpuAsset& skeleton, ResourceFailure& failure) {
    skeleton = {};
    ModelCpuAsset result;
    if (!select_bone || !parse_lnk(links, result.links, failure)) return false;
    if (result.links.empty() || result.links.size() > 4096) {
        fail(failure, ResourceFailureCode::archive_bounds, "visual skeleton node limit");
        return false;
    }
    result.nodes.reserve(result.links.size());
    for (const auto& link : result.links) {
        ResourceBlob selected;
        ModelNodeCpuAsset node;
        node.name = link.child;
        if (!select_bone(link.child, selected, failure)) {
            if (failure.code != ResourceFailureCode::not_found) return false;
            // The shipped loader retains missing-BON nodes. Their transform
            // is not established, so retain identity without inventing one.
            node.transform_available = false;
        } else {
            if (!selected.bytes || (selected.bytes->size() != 12 && selected.bytes->size() != 72 && selected.bytes->size() != 96)) {
                fail(failure, ResourceFailureCode::parse, "BON requires one, six or eight position endpoints");
                return false;
            }
            result.source_bytes += selected.identity.bytes;
            const bool legacy = selected.bytes->size() == 72;
            node.bone_positions.resize(legacy ? 24 : selected.bytes->size()/4);
            // Six-endpoint grace has one shared high-grace endpoint per height.
            // Duplicating it on the strength axis preserves the host polynomial.
            constexpr unsigned old_endpoint[8] = {0, 1, 2, 2, 3, 4, 5, 5};
            for (std::size_t i = 0; i < node.bone_positions.size(); ++i) {
                const auto source = legacy ? old_endpoint[i/3]*3+i%3 : i;
                node.bone_positions[i] = read_float(selected.bytes->data()+4*source);
                if (!std::isfinite(node.bone_positions[i])) {
                    node.bone_positions[i] = 0.0f;
                    bridge_append_probe_event("bon_endpoint_repaired",
                        (visual_reference + "/" + link.child + ": non-finite BON endpoint zeroed").c_str());
                }
            }
        }
        if (link.parent_index != UINT_MAX) {
            if (link.parent_index >= result.nodes.size()) {
                fail(failure, ResourceFailureCode::parse, "visual skeleton parent must precede node");
                return false;
            }
            node.transform_available &= result.nodes[link.parent_index].transform_available;
        }
        result.static_shape_parameterized |= node.bone_positions.size() == 24;
        result.nodes.push_back(std::move(node));
    }
    result.visual_reference = visual_reference;
    result.model_container = links.identity;

    result.source_bytes += links.identity.bytes;
    skeleton = std::move(result);
    failure = {};
    return true;
}

bool decode_model_skeleton_resources(const ResourceBlob& model_blob,
                                    const ResourceBlob& bone_blob,
                                    const std::string& visual_reference,
                                    ModelCpuAsset& skeleton, ResourceFailure& failure) {
    skeleton = {};
    MemoryResourceArchive models, bones;
    ResourceBlob links;
    if (!models.open(model_blob, failure) || !bones.open(bone_blob, failure) ||
        !models.resolve(visual_reference, links, failure)) return false;
    if (!decode_selected_skeleton(links, visual_reference,
        [&](const std::string& name, ResourceBlob& selected, ResourceFailure& error) {
            return bones.resolve(name, selected, error);
        }, skeleton, failure)) return false;
    skeleton.model_container = model_blob.identity;
    skeleton.bone_container = bone_blob.identity;
    skeleton.source_bytes = model_blob.identity.bytes + bone_blob.identity.bytes;
    return true;
}

bool parse_animation_database(const ResourceBlob& blob,
                              AnimationDatabaseAsset& database,
                              ResourceFailure& failure) {
    database = {};
    constexpr std::size_t header_bytes = 44u, entry_bytes = 88u;
    if (!blob.bytes || blob.bytes->size() < header_bytes) {
        fail(failure, ResourceFailureCode::parse, "ADB header is truncated");
        return false;
    }
    // The game never compares the ADB signature.
    if (read_u32(blob.bytes->data()) != 0x00424441u)
        bridge_append_probe_event("adb_signature_ignored",
            (blob.identity.canonical_name + ": ADB signature differs; read as the game does").c_str());
    const std::uint32_t count = read_u32(blob.bytes->data() + 4);
    if (count > 65536u) {
        fail(failure, ResourceFailureCode::archive_bounds, "invalid ADB entry range");
        return false;
    }
    // The game walks the database in place with its stored count and never
    // checks the member size; shipped database.res has ADBs whose last
    // entries end early (inmm1, unmoco3/4, unmozozo). Read every declared
    // entry; bytes past the member read as zero and are logged once.
    const std::uint64_t declared = static_cast<std::uint64_t>(count) * entry_bytes + header_bytes;
    std::vector<std::uint8_t> padded;
    const std::uint8_t* data = blob.bytes->data();
    if (declared != blob.bytes->size()) {
        if (declared > blob.bytes->size()) {
            padded.assign(static_cast<std::size_t>(declared), 0u);
            std::memcpy(padded.data(), data, blob.bytes->size());
            data = padded.data();
        }
        char detail[224];
        std::snprintf(detail, sizeof(detail), "%s: %u entries declared in %zu bytes; read as the game does",
                      blob.identity.canonical_name.c_str(), count, blob.bytes->size());
        bridge_append_probe_event("adb_size_mismatch", detail);
    }
    AnimationDatabaseAsset result{};
    result.identity = blob.identity;
    for (std::size_t i = 0; i < 3; ++i) {
        result.heights[i] = read_float(data + 32u + i * 4u);
        if (!std::isfinite(result.heights[i])) result.heights[i] = 0.0f;  // unchecked by the game
    }
    result.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto* entry = data + header_bytes + i * entry_bytes;
        AnimationEntryAsset value{};
        std::size_t length = 0;
        while (length < 16u && entry[length]) ++length;
        // Shipped selectors index entries by their physical 88-byte position
        // and return database+44+88*n. The stored word at +16 is metadata, not
        // an array-index invariant; valid shipped databases need not equal n.
        // An empty name is kept (the game does not check it); it matches no lookup.
        value.name.assign(reinterpret_cast<const char*>(entry), length);
        value.ordinal = i;
        value.flags = read_u32(entry + 20);
        value.special_flags = read_u32(entry + 24);
        value.length = read_u32(entry + 32);
        value.speed = read_float(entry + 36);
        value.hide_frames = {read_u32(entry + 40), read_u32(entry + 44)};
        for(unsigned k=0;k<4;++k)value.step_frames[k]=read_u32(entry+48+k*4);
        value.hit_frame=read_u32(entry+64);value.sound_frame=read_u32(entry+68);
        if (!std::isfinite(value.speed)) value.speed = 0.0f;  // unchecked by the game
        if (value.length > 0x1000000u) {
            fail(failure, ResourceFailureCode::parse, "invalid ADB duration or rate");
            return false;
        }
        result.entries.push_back(std::move(value));
    }
    database = std::move(result);
    failure = {};
    return true;
}

bool parse_animation_track(const ResourceBlob& blob,
                           AnimationTrackAsset& track,
                           ResourceFailure& failure) {
    track = {};
    failure = {};
    if (!blob.bytes || blob.bytes->size() < 12u) {
        fail(failure, ResourceFailureCode::archive_bounds, "invalid ANM track size");
        return false;
    }
    AnimationTrackAsset result{};
    result.identity = blob.identity;
    std::size_t offset = 0;
    std::uint32_t repaired_frames = 0;
    const auto word = [&](std::uint32_t& value) {
        if (blob.bytes->size() - offset < 4u) return false;
        value = read_u32(blob.bytes->data() + offset);
        offset += 4u;
        return true;
    };
    const auto frames = [&](auto& output, std::uint64_t count) {
        using Frame = typename std::decay_t<decltype(output)>::value_type;
        constexpr std::size_t width = std::tuple_size<Frame>::value;
        if (count > (blob.bytes->size() - offset) / (width * 4u)) return false;
        try {
            output.resize(static_cast<std::size_t>(count));
        } catch (const std::bad_alloc&) {
            fail(failure, ResourceFailureCode::parse, "ANM frame allocation failed");
            return false;
        } catch (const std::length_error&) {
            fail(failure, ResourceFailureCode::parse, "ANM frame allocation failed");
            return false;
        }
        // The game uses frames unchecked. A frame with a non-finite value
        // becomes the identity rotation {w=1,x,y,z=0} or a zero offset.
        for (auto& frame : output) {
            bool finite_frame = true;
            for (float& value : frame) {
                value = read_float(blob.bytes->data() + offset);
                offset += 4u;
                finite_frame = finite_frame && std::isfinite(value);
            }
            if (!finite_frame) {
                frame.fill(0.0f);
                if constexpr (width == 4u) frame[0] = 1.0f;
                ++repaired_frames;
            }
        }
        return true;
    };
    // Shipped human attack tracks can end after a zero morph-frame count.
    // There is no morph payload in that form, so its unused point count is zero.
    std::uint32_t count = 0;
    if (!word(count) || !frames(result.rotations, count) ||
        !word(count) || !frames(result.translations, count) ||
        !word(result.morph_frames) ||
        (!(result.morph_frames == 0 && offset == blob.bytes->size()) &&
         (!word(result.morph_points) ||
          !frames(result.morphs, static_cast<std::uint64_t>(result.morph_frames) *
                                    result.morph_points)))) {
        if (!failure)
            fail(failure, ResourceFailureCode::archive_bounds, "invalid ANM frame range");
        return false;
    }
    // Bytes after the declared frames are never read by the game.
    if (repaired_frames) {
        char detail[224];
        std::snprintf(detail, sizeof(detail), "%s: %u non-finite ANM frames reset",
                      blob.identity.canonical_name.c_str(), repaired_frames);
        bridge_append_probe_event("anm_frames_repaired", detail);
    }
    // Keep the stored quaternion, including its non-unit scale. The owned
    // q*p*conjugate(q) evaluator needs the norm-squared envelope below.
    for (const auto& rotation : result.rotations) {
        double norm = 0;
        for (float value : rotation) norm += static_cast<double>(value) * value;
        result.rotation_scale_bound = (std::max)(result.rotation_scale_bound,
            std::nextafter(static_cast<float>(norm), INFINITY));
    }
    for (std::size_t i = 1; i < result.rotations.size(); ++i) {
        const auto& a = result.rotations[i-1];
        auto b = result.rotations[i];
        double dot = 0;
        for (std::size_t axis = 0; axis < 4; ++axis) dot += static_cast<double>(a[axis])*b[axis];
        if (dot < 0) { for (float& value : b) value = -value; dot = -dot; }
        const double cosine = (std::min)(1.0, dot);
        const double sine = std::sqrt((std::max)(0.0, 1-cosine*cosine));
        if (sine <= 1e-8) continue; // Linear interpolation stays inside endpoint norms.
        // Over the sampled arc, phi is in [0, acos(cosine)] <= pi/2, so
        // |a*cos(phi) + v*sin(phi)| <= |a| + |b-a*cosine|. This also bounds
        // close keys without extending their interpolation to a full circle.
        double aa = 0, vv = 0, av = 0, chord = 0;
        for (std::size_t axis = 0; axis < 4; ++axis) {
            const double delta = b[axis]-a[axis]*cosine;
            const double v = delta/sine;
            aa += static_cast<double>(a[axis])*a[axis];
            vv += v*v;
            av += a[axis]*v;
            chord += delta*delta;
        }
        const double arc = std::sqrt(aa) + std::sqrt(chord);
        const double bound = (std::min)(
            (aa+vv)*0.5 + std::hypot((aa-vv)*0.5, av), arc*arc);
        result.rotation_scale_bound = (std::max)(result.rotation_scale_bound,
            std::nextafter(static_cast<float>(bound), INFINITY));
    }
    const auto radius = [](const auto& values) {
        double squared = 0;
        for (const auto& value : values) {
            double length = 0;
            for (float axis : value) length += static_cast<double>(axis) * axis;
            squared = (std::max)(squared, length);
        }
        return std::nextafter(static_cast<float>(std::sqrt(squared)), INFINITY);
    };
    result.translation_radius = radius(result.translations);
    result.morph_radius = radius(result.morphs);
    if (!std::isfinite(result.rotation_scale_bound) ||
        !std::isfinite(result.translation_radius) || !std::isfinite(result.morph_radius)) {
        fail(failure, ResourceFailureCode::parse, "unbounded ANM track");
        return false;
    }
    track = std::move(result);
    failure = {};
    return true;
}

bool decode_animation_clip(const ResourceBlob& selected_clip,
                           AnimationClipAsset& clip,
                           ResourceFailure& failure, const ResourceStore* budget) {
    clip = {};
    MemoryResourceArchive archive;
    // The game has no track-count limit; the archive's own size bounds it.
    if (!archive.open(selected_clip, failure,budget)) return false;
    AnimationClipAsset result{};
    result.identity = selected_clip.identity;
    const auto names = archive.entry_names();
    if (budget) {
        std::uint64_t bytes = 0;
        for (const auto& name : names) {
            ResourceIdentity identity;
            if (!archive.locate(name,identity,failure)) return false;
            // Frame arrays cannot exceed their validated encoded track. Also
            // charge the retained map value, key, identity and tree links.
            bytes += identity.bytes + sizeof(decltype(result.tracks)::value_type) +
                4*sizeof(void*) + name.size()+1 + identity.canonical_name.size()+1;
        }
        if (bytes > SIZE_MAX) {
            fail(failure,ResourceFailureCode::archive_bounds,"decoded ANM reservation exceeds address space");
            return false;
        }
        result.byte_reservation = budget->allocate_immutable_bytes(0,failure,static_cast<std::size_t>(bytes));
        if (!result.byte_reservation) return false;
    }
    for (const std::string& name : names) {
        ResourceBlob blob{};
        AnimationTrackAsset track{};
        if (!archive.resolve(name, blob, failure,budget) ||
            !parse_animation_track(blob, track, failure)) return false;
        result.tracks.emplace(name, std::move(track));
    }
    clip = std::move(result);
    failure = {};
    return true;
}

namespace {

struct GenerationState {
    OwnedMapLoadRequest request;
    std::uint64_t serial = 0;
    std::int64_t start_tick = 0;
    std::atomic<bool> retired{false};
    std::atomic<std::uint64_t> remaining_jobs{1};
    std::atomic<std::uint64_t> jobs_queued{1};
    std::atomic<std::uint64_t> jobs_completed{0};
    std::atomic<std::uint64_t> jobs_cancelled{0};
    std::atomic<std::uint64_t> worker_cpu_100ns{0};
    std::atomic<std::uint64_t> deduplicated{0};
    std::atomic<std::uint64_t> decoded_source_bytes{0};
    std::shared_ptr<ResourceStore> store;
    std::shared_ptr<MobDocument> mob;
    std::shared_ptr<TerrainCpuAsset> terrain;
    std::vector<StaticObjectSource> objects;
    std::map<std::string, std::shared_ptr<const ModelCpuAsset>> models;
    std::map<std::string, std::shared_ptr<const TextureCpuAsset>> textures;
    std::map<std::string, ResourceFailure> model_failures;
    std::map<std::string, ResourceFailure> texture_failures;
    std::vector<std::string> failures;
    std::mutex result_lock;
};

struct LoaderJob {
    std::shared_ptr<GenerationState> state;
    std::function<void()> action;
};

bool accepted_source(const StaticObjectSource& object) noexcept {
    return object.rejection == StaticObjectRejection::none;
}

std::uint64_t qpc_now() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::int64_t qpc_frequency() noexcept {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) ? value.QuadPart : 0;
    }();
    return frequency;
}

} // namespace

struct OwnedResourceLoader::Impl {
    std::mutex queue_lock;
    std::condition_variable queue_ready;
    std::condition_variable publication_ready;
    std::deque<LoaderJob> queue;
    std::vector<std::thread> workers;
    bool started = false;
    bool stopping = false;
    std::uint64_t next_serial = 0;
    std::shared_ptr<GenerationState> active;
    std::shared_ptr<OwnedMapSnapshot> ready;
#ifdef EID3D11_TESTING
    std::mutex test_lock;
    std::condition_variable test_ready;
    bool test_hold_completion = false;
    bool test_completion_held = false;
    bool test_waiter_entered = false;
#endif

    void enqueue(const std::shared_ptr<GenerationState>& state,
                 std::function<void()> action, bool count_job = true) {
        {
            std::lock_guard<std::mutex> guard(queue_lock);
            if (stopping || state->retired.load(std::memory_order_acquire)) {
                state->jobs_cancelled.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (count_job) {
                state->remaining_jobs.fetch_add(1, std::memory_order_relaxed);
                state->jobs_queued.fetch_add(1, std::memory_order_relaxed);
            }
            queue.push_back({state, std::move(action)});
        }
        queue_ready.notify_one();
    }

    void finish(const std::shared_ptr<GenerationState>& state) {
        state->jobs_completed.fetch_add(1, std::memory_order_relaxed);
        if (state->remaining_jobs.fetch_sub(1, std::memory_order_acq_rel) != 1)
            return;
#ifdef EID3D11_TESTING
        {
            std::unique_lock<std::mutex> guard(test_lock);
            if (test_hold_completion) {
                test_completion_held = true;
                test_ready.notify_all();
                test_ready.wait(guard, [&] { return !test_hold_completion; });
            }
        }
#endif
        if (state->retired.load(std::memory_order_acquire)) {
            if (state->store) state->store->close();
            state->jobs_cancelled.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto snapshot = std::make_shared<OwnedMapSnapshot>();
        snapshot->generation = state->request.generation;
        snapshot->map_resource = state->request.map_resource;
        snapshot->terrain_resource = state->request.terrain_resource;
        snapshot->request_serial = state->serial;
        snapshot->cutover = false;
        {
            std::lock_guard<std::mutex> guard(state->result_lock);
            snapshot->mob = state->mob;
            snapshot->terrain = state->terrain;
            snapshot->objects = state->objects;
            snapshot->models = state->models;
            snapshot->textures = state->textures;
            snapshot->failures = state->failures;
            for (const auto& failure : state->model_failures)
                snapshot->failures.push_back(
                    compact_failure("model", failure.first, failure.second));
            for (const auto& failure : state->texture_failures)
                snapshot->failures.push_back(
                    compact_failure("texture", failure.first, failure.second));
        }
        for (StaticObjectSource& object : snapshot->objects) {
            if (!state->request.load_object_assets || !accepted_source(object)) continue;
            const std::string model_key = fold_ascii(object.visual_reference);
            const std::string texture_key = fold_ascii(object.primary_texture);
            const auto model = snapshot->models.find(model_key);
            const auto texture = snapshot->textures.find(texture_key);
            if (model == snapshot->models.end()) {
                const auto failure = state->model_failures.find(model_key);
                object.rejection = failure != state->model_failures.end() &&
                    failure->second.message.find("animation archive") != std::string::npos
                    ? StaticObjectRejection::animated_asset
                    : StaticObjectRejection::asset_failure;
            } else if (texture == snapshot->textures.end()) {
                object.rejection = StaticObjectRejection::asset_failure;
            }
        }
        if (state->store) {
            snapshot->containers = state->store->containers();
            // The last job has finished reading. Snapshots own their bytes and
            // values; publishing them must not keep a file's mutation lock alive.
            state->store->close();
            snapshot->store_stats = state->store->stats();
        }
        snapshot->metrics.jobs_queued =
            state->jobs_queued.load(std::memory_order_relaxed);
        snapshot->metrics.jobs_completed =
            state->jobs_completed.load(std::memory_order_relaxed);
        snapshot->metrics.jobs_cancelled =
            state->jobs_cancelled.load(std::memory_order_relaxed);
        snapshot->metrics.worker_cpu_ms = static_cast<double>(
            state->worker_cpu_100ns.load(std::memory_order_relaxed)) / 10000.0;
        snapshot->metrics.deduplicated_requests =
            state->deduplicated.load(std::memory_order_relaxed);
        snapshot->metrics.decoded_source_bytes =
            state->decoded_source_bytes.load(std::memory_order_relaxed);
        snapshot->metrics.cold_wall_ms = milliseconds(
            static_cast<std::int64_t>(qpc_now()) - state->start_tick,
            qpc_frequency());
        std::uint64_t owned_bytes = snapshot->mob && snapshot->mob->bytes
            ? snapshot->mob->bytes->size() : 0;
        if (snapshot->terrain)
            for (const TerrainSectorCpuAsset& sector : snapshot->terrain->sectors)
                owned_bytes += sector.vertices.size() * sizeof(TerrainVertex);
        for (const auto& model : snapshot->models)
            owned_bytes += owned_model_bytes(*model.second);
        for (const auto& texture : snapshot->textures)
            if (texture.second->source) owned_bytes += texture.second->source->size();
        snapshot->metrics.peak_owned_bytes = owned_bytes;
        std::sort(snapshot->failures.begin(), snapshot->failures.end());
        snapshot->deterministic_fingerprint = snapshot_fingerprint(*snapshot);
        {
            std::lock_guard<std::mutex> guard(queue_lock);
            if (!stopping && active && active->serial == state->serial &&
                active->request.generation == state->request.generation &&
                !state->retired.load(std::memory_order_relaxed))
                ready = std::move(snapshot);
        }
        publication_ready.notify_all();
    }

    void worker_main() {
        const BackgroundWorkerPriority priority;
        for (;;) {
            LoaderJob job;
            {
                std::unique_lock<std::mutex> guard(queue_lock);
                queue_ready.wait(guard, [&] { return stopping || !queue.empty(); });
                if (stopping && queue.empty()) return;
                job = std::move(queue.front());
                queue.pop_front();
            }
            const std::uint64_t cpu_start = current_thread_cpu_ticks();
            if (job.state->retired.load(std::memory_order_acquire)) {
                job.state->jobs_cancelled.fetch_add(1, std::memory_order_relaxed);
            } else {
                try {
                    job.action();
                } catch (...) {
                    ResourceFailure unexpected{};
                    fail(unexpected, ResourceFailureCode::parse,
                         "loader job raised an unexpected exception");
                    std::lock_guard<std::mutex> guard(job.state->result_lock);
                    job.state->failures.push_back(compact_failure(
                        "job", "unexpected", unexpected));
                }
            }
            const std::uint64_t cpu_end = current_thread_cpu_ticks();
            if (cpu_end >= cpu_start)
                job.state->worker_cpu_100ns.fetch_add(cpu_end - cpu_start,
                                                       std::memory_order_relaxed);
            finish(job.state);
        }
    }

    void schedule_assets(const std::shared_ptr<GenerationState>& state,
                         const std::set<std::string>& model_names,
                         const std::set<std::string>& texture_names) {
        for (const std::string& name : model_names) {
            enqueue(state, [state, name] {
                auto model = std::make_shared<ModelCpuAsset>();
                ResourceFailure failure{};
                if (decode_model_asset(*state->store, name, *model, failure)) {
                    state->decoded_source_bytes.fetch_add(model->source_bytes,
                                                           std::memory_order_relaxed);
                    std::lock_guard<std::mutex> guard(state->result_lock);
                    state->models.emplace(name, std::move(model));
                } else {
                    std::lock_guard<std::mutex> guard(state->result_lock);
                    state->model_failures.emplace(name, std::move(failure));
                }
            });
        }
        for (const std::string& name : texture_names) {
            enqueue(state, [state, name] {
                ResourceBlob blob;
                ResourceFailure failure{};
                auto texture = std::make_shared<TextureCpuAsset>();
                if (state->store->resolve(name + ".mmp", blob, failure) &&
                    parse_texture_asset(blob, *texture, failure)) {
                    state->decoded_source_bytes.fetch_add(blob.identity.bytes,
                                                           std::memory_order_relaxed);
                    std::lock_guard<std::mutex> guard(state->result_lock);
                    state->textures.emplace(name, std::move(texture));
                } else {
                    std::lock_guard<std::mutex> guard(state->result_lock);
                    state->texture_failures.emplace(name, std::move(failure));
                }
            });
        }
    }

    void load_map(const std::shared_ptr<GenerationState>& state) {
        state->store = std::make_shared<ResourceStore>();
        ResourceFailure failure{};
        if (!state->store->open(state->request.mounts, failure)) {
            std::lock_guard<std::mutex> guard(state->result_lock);
            state->failures.push_back(compact_failure("mount", "set", failure));
            return;
        }
        ResourceBlob map_blob;
        if (!state->request.terrain_only && !(state->request.selected_map_file.empty()
                ? state->store->resolve(state->request.map_resource, map_blob, failure)
                : state->store->read_loose_file(state->request.selected_map_file,
                    state->request.map_resource, map_blob, failure))) {
            std::lock_guard<std::mutex> guard(state->result_lock);
            state->failures.push_back(compact_failure(
                "map", state->request.map_resource, failure));
            return;
        }
        if (!state->request.terrain_resource.empty()) {
            ResourceBlob terrain_blob;
            auto terrain = std::make_shared<TerrainCpuAsset>();
            if (!(state->request.selected_terrain_file.empty()
                    ? state->store->resolve(state->request.terrain_resource, terrain_blob, failure)
                    : state->store->read_loose_file(state->request.selected_terrain_file,
                        state->request.terrain_resource, terrain_blob, failure)) ||
                !parse_terrain_asset(terrain_blob,
                                     state->request.terrain_resource,
                                     *terrain, failure)) {
                std::lock_guard<std::mutex> guard(state->result_lock);
                state->failures.push_back(compact_failure(
                    "terrain", state->request.terrain_resource, failure));
                return;
            }
            state->decoded_source_bytes.fetch_add(terrain_blob.identity.bytes,
                                                   std::memory_order_relaxed);
            // Active/menu maps and neighbour maps all publish this asset.
            // Core mode serializes it before slimming the game's copy.
            owned_terrain_prepare_astral(*state->store, *terrain);
            if (state->request.prepare_terrain_shading)
                owned_terrain_prepare_shading(*terrain);
            // The game's terrain surfaces carry one level; their shipped mip
            // chains are read here, under the loading screen.
            const auto page_count = state->request.selected_terrain_file.empty()
                ? terrain->texture_count : static_cast<std::uint32_t>(terrain->texture_resources.size());
            terrain->pages.resize(page_count);
            for (std::uint32_t page = 0; page < page_count &&
                 page < terrain->texture_resources.size(); ++page) {
                auto& output = terrain->pages[page];
                output.store = state->store;
                ResourceFailure page_failure{};
                if (!state->store->locate(terrain->texture_resources[page] + ".mmp",
                                         output.location, page_failure) ||
                    !output.ensure_upload_bytes(page_failure, state->request.selected_terrain_file.empty())) {
                    output = {};
                    continue; // Optional: the renderer reports a page without a chain.
                }
            }
            // Swamp transition frames, from the pages just read (owned_terrain_classify_swamp_frames).
            {
                std::string name = terrain->project_resource;
                std::replace(name.begin(), name.end(), '\\', '/');
                std::string line;
                try { line = owned_terrain_classify_swamp_frames(*terrain); }
                catch (...) { terrain->swamp_frame_cover.clear(); terrain->swamp_frame_texels.clear(); terrain->swamp_frame_side = 0; line = "classification failed (exception)"; }
                bridge_append_probe_event("swamp_frames", (name + ": " + line).c_str());
            }
            owned_terrain_prepare_astral(*state->store, *terrain);
            state->terrain = std::move(terrain);
        } else {
            static std::atomic<bool> reported{false};
            if (!reported.exchange(true))
                bridge_append_probe_event("swamp_frames", "skipped: a map load without a terrain resource (logged once)");
        }
        auto mob = std::make_shared<MobDocument>();
        std::vector<StaticObjectSource> objects;
        if (!state->request.terrain_only && !parse_mob_document(map_blob, *mob, objects, failure,
                                                                    state->request.backdrop_objects)) {
            std::lock_guard<std::mutex> guard(state->result_lock);
            state->failures.push_back(compact_failure(
                "map", state->request.map_resource, failure));
            return;
        }
        state->decoded_source_bytes.fetch_add(map_blob.identity.bytes,
                                               std::memory_order_relaxed);
        std::set<std::string> models;
        std::set<std::string> textures;
        std::set<std::string> allowlist;
        for (const std::string& name : state->request.visual_allowlist)
            allowlist.insert(fold_ascii(name));
        std::set<std::uint32_t> family_allowlist(
            state->request.family_allowlist.begin(),
            state->request.family_allowlist.end());
        std::uint64_t candidate_references = 0;
        for (StaticObjectSource& object : objects) {
            if (!state->request.load_object_assets) continue;
            if (state->request.backdrop_objects &&
                (object.rejection == StaticObjectRejection::script_mutable ||
                 object.rejection == StaticObjectRejection::multipart))
                object.rejection = StaticObjectRejection::none;
            if (!accepted_source(object)) continue;
            if (!family_allowlist.empty() &&
                family_allowlist.find(object.family) == family_allowlist.end()) {
                object.rejection = StaticObjectRejection::outside_asset_slice;
                continue;
            }
            if (!allowlist.empty() &&
                allowlist.find(fold_ascii(object.visual_reference)) ==
                    allowlist.end()) {
                object.rejection = StaticObjectRejection::outside_asset_slice;
                continue;
            }
            ++candidate_references;
            models.insert(fold_ascii(object.visual_reference));
            ++candidate_references;
            textures.insert(fold_ascii(object.primary_texture));
            if (!state->request.selected_terrain_file.empty() && !object.secondary_texture.empty()) {
                ++candidate_references;
                textures.insert(fold_ascii(object.secondary_texture));
            }
        }
        for (const std::string& name : state->request.extra_textures)
            textures.insert(fold_ascii(name));
        const std::uint64_t unique_requests = models.size() + textures.size();
        if (candidate_references > unique_requests)
            state->deduplicated.fetch_add(candidate_references - unique_requests,
                                           std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> guard(state->result_lock);
            state->mob = std::move(mob);
            state->objects = std::move(objects);
        }
        schedule_assets(state, models, textures);
    }
};

OwnedResourceLoader::OwnedResourceLoader() : impl_(std::make_unique<Impl>()) {}
OwnedResourceLoader::~OwnedResourceLoader() { shutdown(); }

bool OwnedResourceLoader::start(std::uint32_t worker_count,
                                ResourceFailure& failure) {
    std::lock_guard<std::mutex> guard(impl_->queue_lock);
    if (impl_->started) {
        failure = {};
        return true;
    }
    worker_count = (std::max)(1u, (std::min)(worker_count, 4u));
    impl_->stopping = false;
    try {
        for (std::uint32_t index = 0; index < worker_count; ++index)
            impl_->workers.emplace_back([this] { impl_->worker_main(); });
    } catch (...) {
        impl_->stopping = true;
        impl_->queue_ready.notify_all();
        // No worker can require queue_lock to exit until this function returns.
        fail(failure, ResourceFailureCode::file_open,
             "bounded loader worker creation failed");
        return false;
    }
    impl_->started = true;
    failure = {};
    return true;
}

bool OwnedResourceLoader::request(const OwnedMapLoadRequest& request,
                                  ResourceFailure& failure,
                                  OwnedResourceRequestToken* token) {
    if (token) *token = {};
    std::shared_ptr<GenerationState> state;
    {
        std::lock_guard<std::mutex> guard(impl_->queue_lock);
        if (!impl_->started || impl_->stopping) {
            fail(failure, ResourceFailureCode::cancelled,
                 "loader is not running");
            return false;
        }
        if (!request.generation || request.map_resource.empty()) {
            fail(failure, ResourceFailureCode::invalid_name,
                 "map request requires a generation and resource name");
            return false;
        }
        if (impl_->active) {
            impl_->active->retired.store(true, std::memory_order_release);
            const auto retired = impl_->active;
            impl_->queue.erase(std::remove_if(
                impl_->queue.begin(), impl_->queue.end(),
                [&](const LoaderJob& job) { return job.state == retired; }),
                impl_->queue.end());
        }
        impl_->ready.reset();
        state = std::make_shared<GenerationState>();
        state->request = request;
        state->serial = ++impl_->next_serial;
        state->start_tick = static_cast<std::int64_t>(qpc_now());
        impl_->active = state;
        impl_->queue.push_back({state, [this, state] { impl_->load_map(state); }});
        if (token) {
            token->generation = request.generation;
            token->request_serial = state->serial;
        }
    }
    impl_->queue_ready.notify_one();
    impl_->publication_ready.notify_all();
    failure = {};
    return true;
}

bool OwnedResourceLoader::try_publish(
    std::shared_ptr<const OwnedMapSnapshot>& snapshot) {
    snapshot.reset();
    const auto start = static_cast<std::int64_t>(qpc_now());
    std::shared_ptr<OwnedMapSnapshot> ready;
    {
        std::lock_guard<std::mutex> guard(impl_->queue_lock);
        if (!impl_->ready) return false;
        ready = std::move(impl_->ready);
    }
    const auto end = static_cast<std::int64_t>(qpc_now());
    ready->metrics.publication_cpu_ms = milliseconds(end - start, qpc_frequency());
    snapshot = std::move(ready);
    return true;
}

bool OwnedResourceLoader::try_publish(
    const OwnedResourceRequestToken& token,
    std::shared_ptr<const OwnedMapSnapshot>& snapshot) {
    snapshot.reset();
    if (!token) return false;
    const auto start = static_cast<std::int64_t>(qpc_now());
    std::shared_ptr<OwnedMapSnapshot> ready;
    {
        std::lock_guard<std::mutex> guard(impl_->queue_lock);
        if (!impl_->ready || impl_->ready->generation != token.generation ||
            impl_->ready->request_serial != token.request_serial)
            return false;
        ready = std::move(impl_->ready);
    }
    const auto end = static_cast<std::int64_t>(qpc_now());
    ready->metrics.publication_cpu_ms = milliseconds(end - start, qpc_frequency());
    snapshot = std::move(ready);
    return true;
}

OwnedResourceWaitResult OwnedResourceLoader::wait_publish(
    const OwnedResourceRequestToken& token,
    std::shared_ptr<const OwnedMapSnapshot>& snapshot) noexcept {
    snapshot.reset();
    if (!token) return OwnedResourceWaitResult::retired;
    std::shared_ptr<OwnedMapSnapshot> ready;
    std::unique_lock<std::mutex> guard(impl_->queue_lock);
#ifdef EID3D11_TESTING
    {
        std::lock_guard<std::mutex> test_guard(impl_->test_lock);
        impl_->test_waiter_entered = true;
    }
    impl_->test_ready.notify_all();
#endif
    impl_->publication_ready.wait(guard, [&] {
        return impl_->stopping || !impl_->started ||
            (impl_->ready && impl_->ready->generation == token.generation &&
             impl_->ready->request_serial == token.request_serial) ||
            !impl_->active || impl_->active->retired.load(std::memory_order_acquire) ||
            impl_->active->request.generation != token.generation ||
            impl_->active->serial != token.request_serial;
    });
    if (impl_->stopping || !impl_->started)
        return OwnedResourceWaitResult::shutdown;
    if (!impl_->ready || impl_->ready->generation != token.generation ||
        impl_->ready->request_serial != token.request_serial)
        return OwnedResourceWaitResult::retired;
    const auto start = static_cast<std::int64_t>(qpc_now());
    ready = std::move(impl_->ready);
    guard.unlock();
    const auto end = static_cast<std::int64_t>(qpc_now());
    ready->metrics.publication_cpu_ms = milliseconds(end - start, qpc_frequency());
    snapshot = std::move(ready);
    return OwnedResourceWaitResult::ready;
}

#ifdef EID3D11_TESTING
bool OwnedResourceLoader::wait_ready_for_test(
    const OwnedResourceRequestToken& token) noexcept {
    if (!token) return false;
    std::unique_lock<std::mutex> guard(impl_->queue_lock);
    impl_->publication_ready.wait(guard, [&] {
        return impl_->stopping || !impl_->started ||
            (impl_->ready && impl_->ready->generation == token.generation &&
             impl_->ready->request_serial == token.request_serial) ||
            !impl_->active || impl_->active->retired.load(std::memory_order_acquire) ||
            impl_->active->request.generation != token.generation ||
            impl_->active->serial != token.request_serial;
    });
    return impl_->ready && impl_->ready->generation == token.generation &&
        impl_->ready->request_serial == token.request_serial;
}

void OwnedResourceLoader::hold_completion_for_test() noexcept {
    std::lock_guard<std::mutex> guard(impl_->test_lock);
    impl_->test_hold_completion = true;
    impl_->test_completion_held = false;
    impl_->test_waiter_entered = false;
}

bool OwnedResourceLoader::wait_completion_held_for_test() noexcept {
    std::unique_lock<std::mutex> guard(impl_->test_lock);
    impl_->test_ready.wait(guard, [&] { return impl_->test_completion_held; });
    return true;
}

bool OwnedResourceLoader::wait_waiter_entered_for_test() noexcept {
    std::unique_lock<std::mutex> guard(impl_->test_lock);
    impl_->test_ready.wait(guard, [&] { return impl_->test_waiter_entered; });
    return true;
}

void OwnedResourceLoader::release_completion_for_test() noexcept {
    {
        std::lock_guard<std::mutex> guard(impl_->test_lock);
        impl_->test_hold_completion = false;
    }
    impl_->test_ready.notify_all();
}
#endif

void OwnedResourceLoader::retire_generation(std::uint64_t generation) noexcept {
    {
        std::lock_guard<std::mutex> guard(impl_->queue_lock);
        if (impl_->active && impl_->active->request.generation == generation) {
            const auto retired = impl_->active;
            retired->retired.store(true, std::memory_order_release);
            impl_->queue.erase(std::remove_if(
                impl_->queue.begin(), impl_->queue.end(),
                [&](const LoaderJob& job) { return job.state == retired; }),
                impl_->queue.end());
            impl_->active.reset();
            impl_->ready.reset();
        }
    }
    impl_->publication_ready.notify_all();
}

void OwnedResourceLoader::shutdown() noexcept {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> guard(impl_->queue_lock);
        if (!impl_->started && impl_->workers.empty()) return;
        if (impl_->active)
            impl_->active->retired.store(true, std::memory_order_release);
        impl_->stopping = true;
        workers.swap(impl_->workers);
    }
    impl_->queue_ready.notify_all();
    impl_->publication_ready.notify_all();
    for (std::thread& worker : workers)
        if (worker.joinable()) worker.join();
    {
        std::lock_guard<std::mutex> guard(impl_->queue_lock);
        impl_->queue.clear();
        impl_->active.reset();
        impl_->ready.reset();
        impl_->started = false;
        impl_->stopping = false;
    }
    impl_->publication_ready.notify_all();
}

bool write_owned_loader_receipt(const OwnedMapSnapshot& snapshot,
                                ResourceFailure& failure) {
    DWORD required = GetEnvironmentVariableW(debug_output_environment_w, nullptr, 0);
    if (!required) {
        fail(failure, ResourceFailureCode::invalid_name,
             "Renderer debug output directory is not configured");
        return false;
    }
    std::wstring directory(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(debug_output_environment_w,
                                                  directory.data(), required);
    if (!copied || copied >= required) {
        fail(failure, ResourceFailureCode::invalid_name,
             "Renderer debug output directory could not be read");
        return false;
    }
    directory.resize(copied);
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fail(failure, ResourceFailureCode::file_open,
             "Renderer debug output directory does not exist");
        return false;
    }
    std::wstring path = directory;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path.push_back(L'\\');
    path += L"owned_loader_generation_";
    path += std::to_wstring(snapshot.generation);
    path += L".json";

    std::array<std::uint64_t,
               static_cast<std::size_t>(StaticObjectRejection::asset_failure) + 1>
        rejection_counts{};
    std::uint64_t accepted = 0;
    for (const StaticObjectSource& object : snapshot.objects) {
        const std::size_t index = static_cast<std::size_t>(object.rejection);
        if (index < rejection_counts.size()) ++rejection_counts[index];
        if (object.rejection == StaticObjectRejection::none) ++accepted;
    }
    std::ostringstream json;
    json.setf(std::ios::fixed);
    json.precision(3);
    json << "{\n"
         << "  \"generation\": " << snapshot.generation << ",\n"
         << "  \"request_serial\": " << snapshot.request_serial << ",\n"
         << "  \"map_resource\": \"" << json_escape(snapshot.map_resource)
         << "\",\n"
         << "  \"terrain_resource\": \""
         << json_escape(snapshot.terrain_resource) << "\",\n"
         << "  \"terrain_loaded\": "
         << (snapshot.terrain ? "true" : "false") << ",\n"
         << "  \"deterministic_fingerprint\": \"" << std::hex
         << snapshot.deterministic_fingerprint << std::dec << "\",\n"
         << "  \"cutover\": false,\n"
         << "  \"containers\": [";
    for (std::size_t index = 0; index < snapshot.containers.size(); ++index) {
        const ResourceContainerInfo& container = snapshot.containers[index];
        if (index) json << ',';
        json << "\n    {\"mount\": " << container.mount_ordinal
             << ", \"kind\": \""
             << (container.loose_file ? "loose" : "res")
             << "\", \"label\": \"" << json_escape(container.label)
             << "\", \"bytes\": " << container.bytes
             << ", \"incarnation\": " << container.incarnation << '}';
    }
    if (!snapshot.containers.empty()) json << '\n';
    json << "  ],\n"
         << "  \"resolved\": {\"loose\": "
         << snapshot.store_stats.resolved_loose << ", \"archive\": "
         << snapshot.store_stats.resolved_archive << ", \"models\": "
         << snapshot.models.size() << ", \"textures\": "
         << snapshot.textures.size() << "},\n"
         << "  \"descriptors\": {\"total\": " << snapshot.objects.size()
         << ", \"accepted_snapshot\": " << accepted << ", \"rejected\": "
         << snapshot.objects.size() - accepted << ", \"reasons\": {";
    bool first_reason = true;
    for (std::size_t index = 1; index < rejection_counts.size(); ++index) {
        if (!rejection_counts[index]) continue;
        if (!first_reason) json << ',';
        first_reason = false;
        json << "\"" << static_object_rejection_name(
            static_cast<StaticObjectRejection>(index)) << "\":"
             << rejection_counts[index];
    }
    json << "}},\n"
         << "  \"timing_ms\": {\"cold_wall\": "
         << snapshot.metrics.cold_wall_ms << ", \"worker_cpu\": "
         << snapshot.metrics.worker_cpu_ms << ", \"publication\": "
         << snapshot.metrics.publication_cpu_ms << "},\n"
         << "  \"jobs\": {\"queued\": " << snapshot.metrics.jobs_queued
         << ", \"completed\": " << snapshot.metrics.jobs_completed
         << ", \"cancelled\": " << snapshot.metrics.jobs_cancelled
         << ", \"deduplicated\": "
         << snapshot.metrics.deduplicated_requests << "},\n"
         << "  \"bytes\": {\"read\": " << snapshot.store_stats.read_bytes
         << ", \"decoded_source\": "
         << snapshot.metrics.decoded_source_bytes << ", \"peak_owned\": "
         << snapshot.metrics.peak_owned_bytes << "},\n"
         << "  \"ownership\": {\"archive_owners\": "
         << snapshot.store_stats.archive_owner_count
         << ", \"mapped_views\": " << snapshot.store_stats.mapped_view_count
         << "},\n"
         << "  \"failures\": [";
    for (std::size_t index = 0; index < snapshot.failures.size(); ++index) {
        if (index) json << ',';
        json << "\"" << json_escape(snapshot.failures[index]) << "\"";
    }
    json << "]\n}\n";
    const std::string body = json.str();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        fail(failure, ResourceFailureCode::file_open,
             "owned loader receipt could not be created");
        return false;
    }
    std::size_t cursor = 0;
    bool ok = true;
    while (cursor < body.size()) {
        const DWORD amount = static_cast<DWORD>((std::min)(
            body.size() - cursor,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(file, body.data() + cursor, amount, &written, nullptr) ||
            written != amount) {
            ok = false;
            break;
        }
        cursor += amount;
    }
    const DWORD write_error = GetLastError();
    CloseHandle(file);
    if (!ok) {
        SetLastError(write_error);
        fail(failure, ResourceFailureCode::file_read,
             "owned loader receipt write failed");
        return false;
    }
    failure = {};
    return true;
}

bool shipped_node_link_matches(const ModelLink& recipe, std::size_t index,
    const void* node, const void* head, const void* game_parent,
    const void* recipe_parent, bool later_root) noexcept {
    if (recipe.parent_index != UINT_MAX)
        return recipe.parent_index < index && game_parent && game_parent == recipe_parent;
    // The first node without a parent name is the head. A later one stays
    // unlinked like a node whose named parent came later (multi-part models
    // such as Dejavu's gz19g wrecks, levers and gates list every part as a root).
    if (recipe.parent.empty() && !later_root) return node == head && !game_parent;
    return node != head && !game_parent;
}

} // namespace owned
