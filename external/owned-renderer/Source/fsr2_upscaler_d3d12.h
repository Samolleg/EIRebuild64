#pragma once
// AMD FSR 2 on the D3D12 backend (renderer_backend=d3d12, x64 host): the
// same programs, pass order, constants and image formats as Fsr2Upscaler
// (fsr2_upscaler.h, whose frame_constants and look-up tables it shares),
// dispatched through the executor's compute slots (d3d12_frame.h) instead of
// a D3D11 context. Resource states and barriers are the executor's.
//
// Inputs (render size): the jittered LDR world colour after post effects,
// the world target's D24 depth, camera + object motion. Output: an
// output-size R8G8B8A8 storage texture.
#include "d3d12_frame.h"
#include "fsr2_upscaler.h"

#include <array>
#include <functional>
#include <vector>

namespace owned {

class Fsr2UpscalerD3D12 {
    using Program = Fsr2Upscaler::Program;
    std::vector<ShaderProgram> programs_;  // Fsr2Upscaler::shader_programs order (stable: the executor keys by them)
    // Render size.
    D3D12TextureRef prepared_color_, previous_depth_, dilated_motion_[2], dilated_depth_, lock_luma_, reactive_masks_, luminance_;
    // Output size.
    D3D12TextureRef lock_status_[2], upscaled_[2], luma_history_[2], new_locks_;
    // 1x1 and the look-up tables.
    D3D12TextureRef spd_counter_, auto_exposure_, default_exposure_, default_reactive_, lanczos_lut_, maximum_bias_lut_;
    UINT render_width_ = 0, render_height_ = 0, output_width_ = 0, output_height_ = 0;
    std::uint32_t frame_ = 0;
    Fsr2Upscaler::History history_;
    bool cleared_ = false;
    std::uint64_t storage_bytes_ = 0;

public:
    // Why FSR cannot run on this executor (null: it can): FSR 2 reads several
    // non-32-bit formats through typed unordered views.
    static const char* unavailable_reason(D3D12Frame& frame) noexcept {
        for (const DXGI_FORMAT format : {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R32G32_FLOAT})
            if (!frame.unordered_load_supported(format)) return "needs typed UAV loads (Direct3D 12)";
        return nullptr;
    }
    bool created(UINT render_width, UINT render_height, UINT output_width, UINT output_height) const {
        return luminance_ && render_width == render_width_ && render_height == render_height_ && output_width == output_width_ &&
               output_height == output_height_;
    }
    std::uint64_t storage_bytes() const { return storage_bytes_; }
    void reset() { *this = Fsr2UpscalerD3D12{}; }

    // The size-dependent images. A failure leaves nothing created.
    HRESULT create(D3D12Frame& frame, UINT rw, UINT rh, UINT ow, UINT oh) {
        reset();
        const HRESULT hr = create_resources(frame, rw, rh, ow, oh);
        if (FAILED(hr)) reset();
        return hr;
    }

    // The frame's textures: colour and motion are render-size targets, depth
    // the world target (its depth is read), output the output-size storage image.
    struct Frame : Fsr2Upscaler::FrameValues {
        const D3D12Texture* color = nullptr;
        const D3D12Texture* depth = nullptr;
        const D3D12Texture* motion = nullptr;
        D3D12Texture* output = nullptr;
    };

    // One upscale, as Fsr2Upscaler::dispatch. pass(name) before each dispatch.
    // S_FALSE: a program's pipeline is not ready (or a ring was exhausted):
    // the output is incomplete this frame and the history starts over.
    HRESULT dispatch(D3D12Frame& d3d12, const Frame& frame, const std::function<void(const char*)>& pass) {
        const bool reset = frame.reset || !cleared_;
        if (!cleared_) { clear_all(d3d12); cleared_ = true; }
        const unsigned p = frame_ & 1u;
        D3D12Texture* lock_read = lock_status_[p ? 1 : 0].get();
        D3D12Texture* lock_write = lock_status_[p ? 0 : 1].get();
        D3D12Texture* upscaled_read = upscaled_[p ? 1 : 0].get();
        D3D12Texture* upscaled_write = upscaled_[p ? 0 : 1].get();
        D3D12Texture* motion_now = dilated_motion_[p ? 1 : 0].get();
        D3D12Texture* motion_before = dilated_motion_[p ? 0 : 1].get();
        D3D12Texture* history_read = luma_history_[p ? 1 : 0].get();
        D3D12Texture* history_write = luma_history_[p ? 0 : 1].get();

        Fsr2Upscaler::Constants c;
        std::uint32_t spd[8];
        Fsr2Upscaler::frame_constants(frame, reset, render_width_, render_height_, output_width_, output_height_, history_, c, spd);
        const std::uint32_t groups_x = (render_width_ - 1) / 64 + 1, groups_y = (render_height_ - 1) / 64 + 1;
        const bool sharpen = frame.sharpness > 0.f;
        std::uint32_t rcas[4]{};
        if (sharpen) Fsr2Upscaler::rcas_constants(frame.sharpness, rcas);

        if (reset) {
            const float zero[4]{}, exposure[4]{-1.f, 1e8f, 0.f, 0.f};
            d3d12.clear_texture(*lock_read, zero);
            d3d12.clear_texture(*upscaled_read, zero);
            d3d12.clear_texture(*luminance_, zero, Fsr2Upscaler::kLuminanceMipShadingChange);
            d3d12.clear_texture(*luminance_, zero, 5);
            d3d12.clear_texture(*auto_exposure_, exposure);
        }

        D3D11_SAMPLER_DESC point{};
        point.AddressU = point.AddressV = point.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        point.ComparisonFunc = D3D11_COMPARISON_NEVER; point.MaxLOD = D3D11_FLOAT32_MAX;
        point.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        D3D11_SAMPLER_DESC linear = point;
        linear.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        const UINT render_x = (render_width_ + 7) / 8, render_y = (render_height_ + 7) / 8;
        const UINT output_x = (output_width_ + 7) / 8, output_y = (output_height_ + 7) / 8;
        // A slot of a dispatch: a texture, the depth of a target, or one level.
        struct View { const D3D12Texture* texture = nullptr; bool depth = false; UINT mip = 0; };
        const auto depth = [](const D3D12Texture* target) { return View{target, true, 0}; };
        const auto level = [](const D3D12Texture* texture, UINT mip) { return View{texture, false, mip}; };
        HRESULT result = S_OK;
        // One dispatch with the views in register order t0.., u0.. (the
        // upstream FSR2_BIND_* numbering), constants b0 (cbFSR2) and b1.
        const auto run = [&](Program program, const void* second, UINT second_bytes, std::initializer_list<View> srvs,
                             std::initializer_list<View> uavs, UINT x, UINT y) {
            if (result != S_OK) return;
            d3d12.clear_slots();
            d3d12.set_constants(0, &c, sizeof(c));
            d3d12.set_constants(1, second, second_bytes);
            d3d12.set_sampler(0, point);
            d3d12.set_sampler(1, linear);
            UINT slot = 0;
            for (const View& view : srvs) {
                if (view.depth) d3d12.set_depth_resource(slot, view.texture);
                else d3d12.set_resource(slot, view.texture);
                ++slot;
            }
            slot = 0;
            for (const View& view : uavs) {
                if (view.mip) d3d12.set_unordered_level(slot, const_cast<D3D12Texture*>(view.texture), view.mip);
                else d3d12.set_unordered(slot, view.texture);
                ++slot;
            }
            result = d3d12.dispatch(programs_[program], x, y, 1);
        };
        const View color{frame.color}, motion{frame.motion}, world_depth = depth(frame.depth);
        const View exposure{default_exposure_.get()}, reactive{default_reactive_.get()};

        pass("scene_fsr_luminance");
        run(Fsr2Upscaler::luminance, spd, sizeof(spd), {color},
            {View{spd_counter_.get()}, level(luminance_.get(), Fsr2Upscaler::kLuminanceMipShadingChange), level(luminance_.get(), 5),
             View{auto_exposure_.get()}},
            groups_x, groups_y);
        pass("scene_fsr_reconstruct");
        run(Fsr2Upscaler::reconstruct, spd, sizeof(spd), {motion, world_depth, color, exposure},
            {View{previous_depth_.get()}, View{motion_now}, View{dilated_depth_.get()}, View{lock_luma_.get()}}, render_x, render_y);
        pass("scene_fsr_depth_clip");
        run(Fsr2Upscaler::depth_clip, spd, sizeof(spd),
            {View{previous_depth_.get()}, View{motion_now}, View{dilated_depth_.get()}, reactive, reactive, View{motion_before}, motion,
             color, world_depth, exposure},
            {View{reactive_masks_.get()}, View{prepared_color_.get()}}, render_x, render_y);
        pass("scene_fsr_lock");
        run(Fsr2Upscaler::lock, spd, sizeof(spd), {View{lock_luma_.get()}}, {View{new_locks_.get()}, View{previous_depth_.get()}},
            render_x, render_y);
        pass("scene_fsr_accumulate");
        run(sharpen ? Fsr2Upscaler::accumulate_sharpen : Fsr2Upscaler::accumulate, spd, sizeof(spd),
            {exposure, View{reactive_masks_.get()}, View{motion_now}, View{upscaled_read}, View{lock_read}, View{prepared_color_.get()},
             View{lanczos_lut_.get()}, View{maximum_bias_lut_.get()}, View{luminance_.get()}, View{auto_exposure_.get()},
             View{history_read}},
            {View{upscaled_write}, View{lock_write}, View{frame.output}, View{new_locks_.get()}, View{history_write}}, output_x, output_y);
        if (sharpen) {
            pass("scene_fsr_rcas");
            run(Fsr2Upscaler::rcas, rcas, sizeof(rcas), {exposure, View{upscaled_write}}, {View{frame.output}},
                (output_width_ + 15) / 16, (output_height_ + 15) / 16);
        }
        d3d12.clear_slots();
        frame_ = (frame_ + 1) & 1u;
        if (result != S_OK) cleared_ = false;  // an incomplete frame: the next one starts the history over
        return result;
    }

private:
    void clear_all(D3D12Frame& d3d12) {
        const float zero[4]{};
        const UINT zero_uint[4]{};
        for (D3D12TextureRef* image : {&prepared_color_, &dilated_motion_[0], &dilated_motion_[1], &dilated_depth_, &lock_luma_,
                                       &reactive_masks_, &lock_status_[0], &lock_status_[1], &upscaled_[0], &upscaled_[1],
                                       &luma_history_[0], &luma_history_[1], &new_locks_, &auto_exposure_, &default_exposure_,
                                       &default_reactive_})
            d3d12.clear_texture(**image, zero);
        d3d12.clear_texture(*luminance_, zero, Fsr2Upscaler::kLuminanceMipShadingChange);
        d3d12.clear_texture(*luminance_, zero, 5);
        d3d12.clear_texture_uint(*previous_depth_, zero_uint);
        d3d12.clear_texture_uint(*spd_counter_, zero_uint);
    }

    static std::uint64_t texel_bytes(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: return 8;
        case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
        case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R8G8_UNORM: return 2;
        default: return 1;
        }
    }
    HRESULT storage(D3D12Frame& d3d12, D3D12TextureRef& out, DXGI_FORMAT format, UINT width, UINT height, UINT mips = 1) {
        const HRESULT hr = d3d12.create_storage_texture(width, height, 0, format, out, mips);
        if (SUCCEEDED(hr))
            for (UINT mip = 0; mip < mips; ++mip)
                storage_bytes_ += std::uint64_t(std::max(1u, width >> mip)) * std::max(1u, height >> mip) * texel_bytes(format);
        return hr;
    }
    HRESULT table(D3D12Frame& d3d12, D3D12TextureRef& out, UINT width, UINT height, const std::int16_t* data) {
        HRESULT hr = d3d12.create_texture(width, height, DXGI_FORMAT_R16_SNORM, out);
        if (SUCCEEDED(hr)) hr = d3d12.write_texture(*out, RECT{0, 0, LONG(width), LONG(height)}, data, width * sizeof(std::int16_t));
        if (SUCCEEDED(hr)) storage_bytes_ += std::uint64_t(width) * height * 2;
        return hr;
    }

    HRESULT create_resources(D3D12Frame& d3d12, UINT rw, UINT rh, UINT ow, UINT oh) {
        if (!rw || !rh || !ow || !oh) return E_INVALIDARG;
        Fsr2Upscaler::shader_programs(programs_);
        if (programs_.size() != Fsr2Upscaler::program_count) return E_FAIL;
        HRESULT hr;
        if (FAILED(hr = storage(d3d12, prepared_color_, DXGI_FORMAT_R16G16B16A16_FLOAT, rw, rh))) return hr;
        if (FAILED(hr = storage(d3d12, previous_depth_, DXGI_FORMAT_R32_UINT, rw, rh))) return hr;
        for (auto& motion : dilated_motion_)
            if (FAILED(hr = storage(d3d12, motion, DXGI_FORMAT_R16G16_FLOAT, rw, rh))) return hr;
        if (FAILED(hr = storage(d3d12, dilated_depth_, DXGI_FORMAT_R32_FLOAT, rw, rh))) return hr;
        if (FAILED(hr = storage(d3d12, lock_luma_, DXGI_FORMAT_R16_FLOAT, rw, rh))) return hr;
        if (FAILED(hr = storage(d3d12, reactive_masks_, DXGI_FORMAT_R8G8_UNORM, rw, rh))) return hr;
        // Luminance: half size, the full chain (UAVs at the shading-change mip and mip 5).
        const UINT lw = std::max(1u, rw / 2), lh = std::max(1u, rh / 2);
        UINT mips = 1;
        while ((std::max(lw, lh) >> mips) > 0) ++mips;
        if (mips <= 5) return E_INVALIDARG;  // the render size is too small for the shading-change mips
        if (FAILED(hr = storage(d3d12, luminance_, DXGI_FORMAT_R16_FLOAT, lw, lh, mips))) return hr;
        for (auto& status : lock_status_)
            if (FAILED(hr = storage(d3d12, status, DXGI_FORMAT_R16G16_FLOAT, ow, oh))) return hr;
        for (auto& color : upscaled_)
            if (FAILED(hr = storage(d3d12, color, DXGI_FORMAT_R16G16B16A16_FLOAT, ow, oh))) return hr;
        for (auto& history : luma_history_)
            if (FAILED(hr = storage(d3d12, history, DXGI_FORMAT_R8G8B8A8_UNORM, ow, oh))) return hr;
        if (FAILED(hr = storage(d3d12, new_locks_, DXGI_FORMAT_R8_UNORM, ow, oh))) return hr;
        if (FAILED(hr = storage(d3d12, spd_counter_, DXGI_FORMAT_R32_UINT, 1, 1))) return hr;
        if (FAILED(hr = storage(d3d12, auto_exposure_, DXGI_FORMAT_R32G32_FLOAT, 1, 1))) return hr;
        // The default exposure (0: exposure 1) and reactive mask (0): cleared once with the rest.
        if (FAILED(hr = storage(d3d12, default_exposure_, DXGI_FORMAT_R32G32_FLOAT, 1, 1))) return hr;
        if (FAILED(hr = storage(d3d12, default_reactive_, DXGI_FORMAT_R8_UNORM, 1, 1))) return hr;
        std::int16_t lanczos[128];
        Fsr2Upscaler::lanczos_table(lanczos);
        if (FAILED(hr = table(d3d12, lanczos_lut_, 128, 1, lanczos))) return hr;
        std::int16_t bias[Fsr2Upscaler::kMaximumBiasTexels];
        Fsr2Upscaler::maximum_bias_table(bias);
        if (FAILED(hr = table(d3d12, maximum_bias_lut_, FFX_FSR2_MAXIMUM_BIAS_TEXTURE_WIDTH, FFX_FSR2_MAXIMUM_BIAS_TEXTURE_HEIGHT, bias)))
            return hr;
        render_width_ = rw; render_height_ = rh; output_width_ = ow; output_height_ = oh;
        return S_OK;
    }
};

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
