#include "renderer.h"
#include "surface_convert.h"
#include "texture_mips.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

namespace owned {
namespace {
#ifdef EID3D11_TESTING
bool fail_next_surface_srv_creation = false;
#endif
} // namespace

bool surface_dxgi_format(const DDPIXELFORMAT& format, DXGI_FORMAT& output,
                         UINT& block_bytes) noexcept {
    return surface_convert::target_format(format, output, block_bytes);
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


using PreparedUpload = surface_convert::Prepared;

HRESULT prepare_upload(const SurfaceImage& image, const RECT& rect,
                       UINT block_bytes, PreparedUpload& output) {
    surface_convert::Source source;
    source.pixels = image.pixels.data();
    source.pitch = static_cast<std::size_t>(image.pitch);
    source.format = image.format;
    source.has_color_key = image.has_color_key;
    source.color_key = image.color_key;
    return surface_convert::prepare(source, rect, block_bytes, output);
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

HRESULT ensure_surface_texture(ID3D11Device* device, SurfaceImage& image, bool& write_needed, bool& recreated) {
    write_needed = recreated = false;
    if (!device || !image.width || !image.height || image.pitch <= 0) return E_INVALIDARG;
    DXGI_FORMAT target_format = DXGI_FORMAT_UNKNOWN;
    UINT block_bytes = 0;
    if (!surface_dxgi_format(image.format, target_format, block_bytes)) return E_NOTIMPL;
    const bool generate_mips = image.static_mip_hint && image.world_mip_use && !image.gpu_captured && !block_bytes;
    const bool texture_matches = same_surface_upload_shape(image.uploaded_shape, surface_upload_shape(image)) &&
        matching_texture(image.texture.Get(), image.width, image.height, target_format, generate_mips);
    if (texture_matches && image.srv && image.uploaded_generation == image.generation) return S_OK;
    if (!texture_matches) {
        // The description upload_surface_image creates, without content.
        D3D11_TEXTURE2D_DESC description{};
        description.Width = image.width;
        description.Height = image.height;
        description.MipLevels = generate_mips ? full_mip_count(image.width, image.height) : 1;
        description.ArraySize = 1;
        description.Format = target_format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (generate_mips) { description.BindFlags |= D3D11_BIND_RENDER_TARGET; description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS; }
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = device->CreateTexture2D(&description, nullptr, &texture);
        if (FAILED(hr)) return hr;
        hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (FAILED(hr)) return hr;
        image.texture = texture;
        image.srv = srv;
        ++image.texture_serial;
        recreated = true;
    } else if (!image.srv) {
        const HRESULT hr = device->CreateShaderResourceView(image.texture.Get(), nullptr, &image.srv);
        if (FAILED(hr)) return hr;
    }
    write_needed = true;
    return S_OK;
}

HRESULT prepare_surface_upload(SurfaceImage& image, bool full, UINT block_bytes, RECT& rect,
                               PreparedUpload& output) {
    if (!image.width || !image.height || image.pitch <= 0) return E_INVALIDARG;
    const UINT source_rows = block_bytes ? std::max<UINT>(1, (image.height + 3) / 4) : image.height;
    const std::size_t required = static_cast<std::size_t>(image.pitch) * source_rows;
    try {
        if (image.pixels.size() < required) image.pixels.resize(required);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    rect = normalize_dirty_rect(image, full, block_bytes);
    return prepare_upload(image, rect, block_bytes, output);
}

HRESULT upload_surface_image(ID3D11Device* device, ID3D11DeviceContext* context,
                             SurfaceImage& image, SurfaceUploadResult* result,
                             const PreparedUpload* prepared, const RECT* prepared_rect) {
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
    PreparedUpload converted{};
    HRESULT hr = S_OK;
    const bool use_prepared = prepared && prepared_rect && !recreate &&
        std::memcmp(prepared_rect, &dirty, sizeof(RECT)) == 0;
    if (!use_prepared && FAILED(hr = prepare_upload(image, dirty, block_bytes, converted))) return hr;
    const PreparedUpload& upload = use_prepared ? *prepared : converted;

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
        // Direct3D 11 runtime defect (documented for UpdateSubresource): on
        // a deferred context whose driver has no command lists, the runtime
        // adds the destination box's offset to the source pointer. The game
        // thread uploads on a deferred context (Renderer::game_upload); a
        // tester's GTX 1080 crashed in d3d11.dll reading upload.data +
        // (top/4)*pitch + (left/4)*8 for a BC1 rectangle. There the pointer
        // is moved back by that offset (elements: 4x4 blocks or 32-bit texels).
        const void* source = upload.data;
        if (box_pointer && context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) {
            D3D11_FEATURE_DATA_THREADING threading{};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading))) &&
                !threading.DriverCommandLists) {
                const std::size_t rows = block_bytes ? box.top / 4u : box.top;
                const std::size_t offset = block_bytes ? std::size_t(box.left / 4u) * block_bytes
                                                       : std::size_t(box.left) * 4u;
                source = static_cast<const std::uint8_t*>(upload.data) - rows * upload.pitch - offset;
            }
        }
        context->UpdateSubresource(texture.Get(), 0, box_pointer,
                                   source, upload.pitch, 0);
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
