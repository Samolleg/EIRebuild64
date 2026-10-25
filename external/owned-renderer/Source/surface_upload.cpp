#include "renderer.h"
#include "surface_convert.h"
#include "texture_mips.h"
#include "x64_remote_surfaces.h"

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

// Remote mode: the same state transitions as upload_surface_image, but the
// host converts and writes (surface protocol). Only the texture and view
// creations stay proxy device calls (client handles); the content steps go
// out as SurfaceStep records, queued for the scene thread's commit when
// `queued`, else applied at once in ring order.
HRESULT upload_surface_image_remote(ID3D11Device* device, bool queued,
                                    SurfaceImage& image, SurfaceUploadResult* result) {
    if (result) *result = {};
    if (!device || !image.width || !image.height || image.pitch <= 0) return E_INVALIDARG;

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

    if (image.cpu_released) {
        // The texture is the only copy (see upload_surface_image): only the
        // mip layout changed, so level 0 is copied on the host.
        if (!shape_matches || image.uploaded_generation != image.generation || !image.texture)
            return E_UNEXPECTED;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = device->CreateTexture2D(&description, nullptr, &texture);
        if (FAILED(hr)) return hr;
        hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (FAILED(hr)) return hr;
        x64remote::surface_copy(queued, texture.Get(), srv.Get(), image.texture.Get(), false, generate_mips);
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
    ComPtr<ID3D11Texture2D> texture = image.texture;
    ComPtr<ID3D11ShaderResourceView> srv = image.srv;
    if (recreate) {
        texture.Reset();
        srv.Reset();
        HRESULT hr = device->CreateTexture2D(&description, nullptr, &texture);
        if (FAILED(hr)) return hr;
        hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (FAILED(hr)) return hr;
    } else if (!srv) {
        const HRESULT hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        if (FAILED(hr)) return hr;
    }

    // Read-only access: a shared (copy-on-write) pixel snapshot is not detached.
    const std::uint8_t* const pixels = static_cast<const SurfaceImage&>(image).pixels.data();
    const UINT width = static_cast<UINT>(dirty.right - dirty.left);
    const UINT height = static_cast<UINT>(dirty.bottom - dirty.top);
    const UINT source_bytes = std::max<UINT>(1, image.format.dwRGBBitCount / 8);
    x64remote::SurfaceWrite write{};
    write.texture = texture.Get();
    write.srv = srv.Get();
    write.x = static_cast<UINT>(dirty.left);
    write.y = static_cast<UINT>(dirty.top);
    write.width = width;
    write.height = height;
    write.source_pitch = static_cast<UINT>(image.pitch);
    if (block_bytes) {
        write.source = pixels + static_cast<std::size_t>(dirty.top / 4) * image.pitch +
            static_cast<std::size_t>(dirty.left / 4) * block_bytes;
        write.row_bytes = (width + 3) / 4 * block_bytes;
        write.rows = (height + 3) / 4;
    } else {
        write.source = pixels + static_cast<std::size_t>(dirty.top) * image.pitch +
            static_cast<std::size_t>(dirty.left) * source_bytes;
        write.row_bytes = width * source_bytes;
        write.rows = height;
    }
    write.format = &image.format;
    write.color_key = image.has_color_key ? &image.color_key : nullptr;
    write.generate_mips = generate_mips;
    x64remote::surface_write(queued, write);

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
        result->upload_bytes = block_bytes
            ? static_cast<std::uint64_t>(write.row_bytes) * write.rows
            : static_cast<std::uint64_t>(width) * height * 4u;
    }
    return S_OK;
}

} // namespace owned
