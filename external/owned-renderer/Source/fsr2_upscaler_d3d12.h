#pragma once
// RCAS sharpening after DLSS on the D3D12 backend (renderer_backend=d3d12,
// x64 host), from the FSR 2 RCAS program (fsr2_upscaler.h). FSR itself runs
// on D3D12 as FidelityFX FSR 3.1 in the render host (D3D12Frame::fsr_upscale).
#include "d3d12_frame.h"
#include "fsr2_upscaler.h"

#include <array>
#include <functional>
#include <vector>

namespace owned {

// UpscaleSharpen on the D3D12 backend: RCAS over the output-size image after
// DLSS, in place (the image copied to a scratch texture RCAS reads).
class UpscaleSharpenD3D12 {
    ShaderProgram program_;
    D3D12TextureRef exposure_, copy_;
    UINT width_ = 0, height_ = 0;

public:
    void reset() { *this = UpscaleSharpenD3D12{}; }
    std::uint64_t storage_bytes() const { return copy_ ? std::uint64_t(width_) * height_ * 4u : 0u; }
    // image: a 2D storage texture (R8G8B8A8). S_FALSE: the pipeline is not ready.
    HRESULT apply(D3D12Frame& d3d12, D3D12Texture& image, float sharpness) {
        if (!(sharpness > 0.f)) return S_FALSE;
        UINT width = 0, height = 0, mips = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        if (!d3d12.texture_info(image, width, height, mips, format)) return E_INVALIDARG;
        HRESULT hr;
        if (!copy_ || width != width_ || height != height_) {
            reset();
            program_ = Fsr2Upscaler::rcas_program();
            if (FAILED(hr = d3d12.create_storage_texture(1, 1, 0, DXGI_FORMAT_R32G32_FLOAT, exposure_)) ||
                FAILED(hr = d3d12.create_storage_texture(width, height, 0, format, copy_))) { reset(); return hr; }
            const float zero[4]{};
            d3d12.clear_texture(*exposure_, zero);  // exposure 0: FSR's default (exposure 1)
            width_ = width; height_ = height;
        }
        // cbFSR2: RCAS reads the display size (its edge clamp) and the pre-exposure (1).
        Fsr2Upscaler::Constants c{};
        c.display_size[0] = std::int32_t(width);
        c.display_size[1] = std::int32_t(height);
        c.pre_exposure = c.previous_pre_exposure = 1.f;
        std::uint32_t rcas[4];
        Fsr2Upscaler::rcas_constants(sharpness, rcas);
        if (FAILED(hr = d3d12.copy_texture(*copy_, 0, image, 0))) return hr;
        d3d12.clear_slots();
        d3d12.set_constants(0, &c, sizeof(c));
        d3d12.set_constants(1, rcas, sizeof(rcas));
        d3d12.set_resource(0, exposure_.get());
        d3d12.set_resource(1, copy_.get());
        d3d12.set_unordered(0, &image);
        hr = d3d12.dispatch(program_, (width + 15) / 16, (height + 15) / 16, 1);  // 16x16 pixels a group
        d3d12.clear_slots();
        return hr;
    }
};

} // namespace owned
