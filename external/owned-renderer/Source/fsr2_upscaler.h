#pragma once
// AMD FidelityFX Super Resolution 2.2.1 on D3D11 (player option dlss_mode
// 6..10, dlss_frame.h). AMD ships FSR2 for D3D12 and Vulkan only; this is an
// independently written D3D11 dispatcher around AMD's unmodified MIT shaders
// (ThirdParty/fsr2, embedded by Tools/embed-fsr2.py, compiled cs_5_0 by the
// renderer's shader table). It follows the upstream host code's pass order,
// constants and resource formats (ffx_fsr2.cpp, v2.2.1) and runs the same in
// the in-process 32-bit device and the 64-bit render host.
//
// Inputs (render size): the jittered LDR world colour after post effects,
// the D24 world depth (not inverted), camera + object motion in render
// pixels from the unjittered current to the previous position. No reactive
// or transparency masks (the 1x1 default zero masks), exposure 1. Output:
// the output-size R8G8B8A8 image scene_composite presents.
//
// Every resource is GPU-only (DEFAULT usage, the three look-up tables
// IMMUTABLE from transient data): no CPU-side copies in the 32-bit process.
// storage_bytes() is counted in the renderer's screen memory.
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "shader_cache.h"
#include "fsr2_embedded.h"
#include "../ThirdParty/fsr2/ffx_fsr2_maximum_bias.h"

namespace owned {

class Fsr2Upscaler {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    // FSR2_PASS of the embedded source (Tools/embed-fsr2.py order), plus the
    // accumulate variant that leaves sharpening to RCAS.
    enum Program { luminance, reconstruct, depth_clip, lock, accumulate, rcas, accumulate_sharpen, program_count };
    struct Image {
        Ptr<ID3D11Texture2D> texture;
        Ptr<ID3D11ShaderResourceView> srv;
        Ptr<ID3D11UnorderedAccessView> uav;
    };
    // Constant buffer cbFSR2 (ffx_fsr2_callbacks_hlsl.h); field order and
    // sizes match the HLSL packing (124 bytes, no float2 crosses 16 bytes).
    struct Constants {
        std::int32_t render_size[2], max_render_size[2], display_size[2], input_color_size[2], luma_mip_size[2];
        std::int32_t luma_mip_level, frame_index;
        float device_to_view_depth[4], jitter[2], motion_vector_scale[2], downscale_factor[2], jitter_cancellation[2];
        float pre_exposure, previous_pre_exposure, tan_half_fov, jitter_phase_count, delta_time, dynamic_res_change,
              view_to_meters;
    };
    static_assert(sizeof(Constants) == 124, "cbFSR2 layout");
    Ptr<ID3D11ComputeShader> programs_[program_count];
    Ptr<ID3D11Buffer> constants_, spd_constants_, rcas_constants_;
    Ptr<ID3D11SamplerState> point_, linear_;
    // Render size: prepared colour, reconstructed previous depth, dilated
    // motion (two, current/previous), dilated depth, lock input luma, dilated
    // reactive masks, luminance mips (half size, full chain; UAVs at mips 4, 5).
    Image prepared_color_, previous_depth_, dilated_motion_[2], dilated_depth_, lock_luma_, reactive_masks_, luminance_;
    Ptr<ID3D11UnorderedAccessView> luminance_mip4_, luminance_mip5_;
    // Output size: lock status, upscaled colour and luma history (two each,
    // ping-pong), new locks.
    Image lock_status_[2], upscaled_[2], luma_history_[2], new_locks_;
    // 1x1: SPD atomic counter, auto exposure (tracked for the lock logic),
    // default exposure (0: exposure 1) and default reactive mask (0).
    Image spd_counter_, auto_exposure_, default_exposure_, default_reactive_;
    Image lanczos_lut_, maximum_bias_lut_;
    UINT render_width_ = 0, render_height_ = 0, output_width_ = 0, output_height_ = 0;
    std::uint32_t frame_ = 0;          // ping-pong parity source (resourceFrameIndex)
    // The constants' state across frames (frame_constants).
    struct History {
        std::int32_t frame_index = 0;     // frames since the last reset
        float jitter_phase_count = 0.f;
    } history_;
    bool cleared_ = false;             // every image cleared once after creation
    std::uint64_t storage_bytes_ = 0;

    static constexpr UINT kLuminanceMipShadingChange = 4;  // FFX_FSR2_SHADING_CHANGE_MIP_LEVEL

    static const std::string& source() {
        static const std::string text = fsr2_shader_library();
        return text;
    }
    // The RCAS pass's source: the same library with its input load clamped
    // to the image (DisplaySize), so the edge rows and columns sample their
    // own border instead of the zero an out-of-range load returns. Used by
    // FSR's own sharpening and by UpscaleSharpen after DLSS.
    static const std::string& rcas_source() {
        static const std::string text = [] {
            std::string library = fsr2_shader_library();
            static constexpr char kLoad[] = "return r_rcas_input[iPxPos];";
            const std::size_t at = library.find(kLoad);
            if (at != std::string::npos)
                library.replace(at, sizeof(kLoad) - 1,
                                "return r_rcas_input[clamp(iPxPos, FfxInt32x2(0, 0), DisplaySize() - FfxInt32x2(1, 1))];");
            return library;
        }();
        return text;
    }
    static const std::string& program_source(int program) { return program == rcas ? rcas_source() : source(); }
    static const D3D_SHADER_MACRO* defines(Program program) {
        // Shared options: FP32 (cs_5_0 has no 16-bit types), SPD without wave
        // intrinsics, render-size unjittered motion, Lanczos look-up table,
        // LDR colour, standard depth.
#define EID3D11_FSR2_COMMON {"FFX_GPU","1"},{"FFX_HLSL","1"},{"FFX_HALF","0"},{"SPD_NO_WAVE_OPERATIONS","1"}, \
    {"FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS","1"},{"FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS","0"}, \
    {"FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE","1"},{"FFX_FSR2_OPTION_HDR_COLOR_INPUT","0"}, \
    {"FFX_FSR2_OPTION_INVERTED_DEPTH","0"}
        static const D3D_SHADER_MACRO table[program_count][12] = {
            {{"FSR2_PASS","0"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","0"},{nullptr,nullptr}},
            {{"FSR2_PASS","1"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","0"},{nullptr,nullptr}},
            {{"FSR2_PASS","2"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","0"},{nullptr,nullptr}},
            {{"FSR2_PASS","3"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","0"},{nullptr,nullptr}},
            {{"FSR2_PASS","4"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","0"},{nullptr,nullptr}},
            {{"FSR2_PASS","5"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","0"},{nullptr,nullptr}},
            {{"FSR2_PASS","4"},EID3D11_FSR2_COMMON,{"FFX_FSR2_OPTION_APPLY_SHARPENING","1"},{nullptr,nullptr}},
        };
#undef EID3D11_FSR2_COMMON
        return table[program];
    }

public:
    static void shader_programs(std::vector<ShaderProgram>& out) {
        for (int program = 0; program < program_count; ++program)
            out.push_back({program_source(program), "FSR2", defines(Program(program)), "CS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    }
    // The RCAS program alone (UpscaleSharpen); one of shader_programs.
    static ShaderProgram rcas_program() {
        return {rcas_source(), "FSR2", defines(rcas), "CS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
    }
    // FsrRcasCon's first value for a 0..1 strength (the upstream remap:
    // sharpness 1 = 0 stops); the FP32 path reads only that value.
    static void rcas_constants(float sharpness, std::uint32_t (&out)[4]) {
        const float stops = 2.f - 2.f * std::clamp(sharpness, 0.f, 1.f);
        const float linear = std::exp2(-stops);
        out[0] = out[1] = out[2] = out[3] = 0;
        std::memcpy(&out[0], &linear, 4);
    }

    // Why FSR cannot run on this device (null: it can). FSR2 reads several
    // non-32-bit UAV formats (D3D11.3 typed UAV loads).
    static const char* unavailable_reason(ID3D11Device* device) noexcept {
        if (!device) return "no graphics device";
        if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) return "needs a Direct3D 11.0 graphics card";
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
        if (FAILED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof(options))) ||
            !options.TypedUAVLoadAdditionalFormats)
            return "needs typed UAV loads (Direct3D 11.3)";
        for (const DXGI_FORMAT format : {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R32G32_FLOAT}) {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{format, 0};
            if (FAILED(device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) ||
                !(support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
                return "needs typed UAV loads (Direct3D 11.3)";
        }
        return nullptr;
    }

    bool created(UINT render_width, UINT render_height, UINT output_width, UINT output_height) const {
        return programs_[0] && render_width == render_width_ && render_height == render_height_ &&
               output_width == output_width_ && output_height == output_height_;
    }
    std::uint64_t storage_bytes() const { return storage_bytes_; }
    void reset() { *this = Fsr2Upscaler{}; }

    // Programs (first call) and the size-dependent images. A failure leaves
    // nothing created.
    HRESULT create(ID3D11Device* device, UINT render_width, UINT render_height, UINT output_width, UINT output_height) {
        reset();
        HRESULT hr = create_resources(device, render_width, render_height, output_width, output_height);
        if (FAILED(hr)) reset();
        return hr;
    }

    // The frame's values, the same for both backends (fsr2_upscaler_d3d12.h).
    struct FrameValues {
        float jitter_x = 0, jitter_y = 0;            // viewport offset of this frame (pixels, right/down)
        float jitter_phases = 8;                     // length of the jitter sequence
        float near_plane = 1, far_plane = 1000, tan_half_fov_y = 1;
        float delta_seconds = 0;
        float sharpness = 0;                         // RCAS 0 (off) .. 1
        bool reset = false;
    };
    struct Frame : FrameValues {
        ID3D11ShaderResourceView* color = nullptr;   // render size, jittered
        ID3D11ShaderResourceView* depth = nullptr;   // render size, device depth 0 near .. 1 far
        ID3D11ShaderResourceView* motion = nullptr;  // render size, RG pixels, current -> previous
        ID3D11UnorderedAccessView* output = nullptr; // output size
    };

    // cbFSR2 and the SPD constants of one frame (ffx_fsr2.cpp fsr2Dispatch,
    // ffx_spd.h SpdSetup); history carries the frame index and phase count.
    static void frame_constants(const FrameValues& frame, bool reset, UINT render_width, UINT render_height, UINT output_width,
                                UINT output_height, History& history, Constants& c, std::uint32_t (&spd)[8]) {
        c = Constants{};
        c.render_size[0] = c.max_render_size[0] = c.input_color_size[0] = std::int32_t(render_width);
        c.render_size[1] = c.max_render_size[1] = c.input_color_size[1] = std::int32_t(render_height);
        c.display_size[0] = std::int32_t(output_width);
        c.display_size[1] = std::int32_t(output_height);
        c.luma_mip_level = std::int32_t(kLuminanceMipShadingChange);
        c.luma_mip_size[0] = std::int32_t(float(render_width) / float(2u << kLuminanceMipShadingChange));
        c.luma_mip_size[1] = std::int32_t(float(render_height) / float(2u << kLuminanceMipShadingChange));
        history.frame_index = reset ? 0 : history.frame_index + 1;
        c.frame_index = history.frame_index;
        // Device depth to view depth for a standard (not inverted, finite)
        // left-handed projection: c, e of the projection's z row.
        const float aspect = float(render_width) / float(render_height);
        const float q = frame.far_plane / (frame.near_plane - frame.far_plane);
        c.device_to_view_depth[0] = -q;
        c.device_to_view_depth[1] = q * frame.near_plane;
        c.device_to_view_depth[2] = frame.tan_half_fov_y * aspect;
        c.device_to_view_depth[3] = frame.tan_half_fov_y;
        c.jitter[0] = frame.jitter_x;
        c.jitter[1] = frame.jitter_y;
        c.motion_vector_scale[0] = 1.f / float(render_width);
        c.motion_vector_scale[1] = 1.f / float(render_height);
        c.downscale_factor[0] = float(render_width) / float(output_width);
        c.downscale_factor[1] = float(render_height) / float(output_height);
        c.pre_exposure = c.previous_pre_exposure = 1.f;
        c.tan_half_fov = frame.tan_half_fov_y * aspect;  // horizontal
        // The phase count follows a changed sequence one step per frame.
        if (reset || history.jitter_phase_count == 0.f) history.jitter_phase_count = frame.jitter_phases;
        else if (frame.jitter_phases > history.jitter_phase_count) history.jitter_phase_count += 1.f;
        else if (frame.jitter_phases < history.jitter_phase_count) history.jitter_phase_count -= 1.f;
        c.jitter_phase_count = history.jitter_phase_count;
        c.delta_time = std::clamp(frame.delta_seconds, 0.f, 1.f);
        c.view_to_meters = 1.f;
        // SPD over the render rectangle.
        const std::uint32_t groups_x = (render_width - 1) / 64 + 1, groups_y = (render_height - 1) / 64 + 1;
        const std::uint32_t values[8] = {
            std::uint32_t(std::min(std::floor(std::log2(float(std::max(render_width, render_height)))), 12.f)),
            groups_x * groups_y, 0, 0, render_width, render_height, 0, 0};
        std::memcpy(spd, values, sizeof(values));
    }
    // The R16_SNORM look-up tables (ffx_fsr2.cpp fsr2Create): Lanczos-2
    // weights over [0, 2], and the upsample maximum bias.
    static void lanczos_table(std::int16_t (&out)[128]) {
        for (int i = 0; i < 128; ++i) {
            const float x = 2.f * float(i) / 127.f, pi = 3.14159265358979f;
            const float y = std::fabs(x) < 1e-6f ? 1.f : (std::sin(pi * x) / (pi * x)) * (std::sin(.5f * pi * x) / (.5f * pi * x));
            out[i] = std::int16_t(std::lround(y * 32767.f));
        }
    }
    static constexpr int kMaximumBiasTexels = FFX_FSR2_MAXIMUM_BIAS_TEXTURE_WIDTH * FFX_FSR2_MAXIMUM_BIAS_TEXTURE_HEIGHT;
    static void maximum_bias_table(std::int16_t (&out)[kMaximumBiasTexels]) {
        for (int i = 0; i < kMaximumBiasTexels; ++i) out[i] = std::int16_t(std::lround(ffxFsr2MaximumBias[i] / 2.f * 32767.f));
    }

    // One upscale. pass(name) is called before each dispatch (profiler pass
    // boundaries). Leaves no compute stage resource bound.
    void dispatch(ID3D11DeviceContext* context, const Frame& frame, const std::function<void(const char*)>& pass) {
        const bool reset = frame.reset || !cleared_;
        if (!cleared_) { clear_all(context); cleared_ = true; }
        const unsigned p = frame_ & 1u;
        Image& lock_read = lock_status_[p ? 1 : 0];
        Image& lock_write = lock_status_[p ? 0 : 1];
        Image& upscaled_read = upscaled_[p ? 1 : 0];
        Image& upscaled_write = upscaled_[p ? 0 : 1];
        Image& motion_now = dilated_motion_[p ? 1 : 0];
        Image& motion_before = dilated_motion_[p ? 0 : 1];
        Image& history_read = luma_history_[p ? 1 : 0];
        Image& history_write = luma_history_[p ? 0 : 1];

        Constants c;
        std::uint32_t spd[8];
        frame_constants(frame, reset, render_width_, render_height_, output_width_, output_height_, history_, c, spd);
        write(context, constants_.Get(), &c, sizeof(c));
        write(context, spd_constants_.Get(), spd, sizeof(spd));
        const std::uint32_t groups_x = (render_width_ - 1) / 64 + 1, groups_y = (render_height_ - 1) / 64 + 1;

        const bool sharpen = frame.sharpness > 0.f;
        if (sharpen) {
            std::uint32_t rcas[4];
            rcas_constants(frame.sharpness, rcas);
            write(context, rcas_constants_.Get(), rcas, sizeof(rcas));
        }

        if (reset) {
            const float zero[4]{}, exposure[4]{-1.f, 1e8f, 0.f, 0.f};
            context->ClearUnorderedAccessViewFloat(lock_read.uav.Get(), zero);
            context->ClearUnorderedAccessViewFloat(upscaled_read.uav.Get(), zero);
            context->ClearUnorderedAccessViewFloat(luminance_mip4_.Get(), zero);
            context->ClearUnorderedAccessViewFloat(luminance_mip5_.Get(), zero);
            context->ClearUnorderedAccessViewFloat(auto_exposure_.uav.Get(), exposure);
        }

        ID3D11Buffer* buffers[2] = {constants_.Get(), spd_constants_.Get()};
        context->CSSetConstantBuffers(0, 2, buffers);
        ID3D11SamplerState* samplers[2] = {point_.Get(), linear_.Get()};
        context->CSSetSamplers(0, 2, samplers);
        const UINT render_x = (render_width_ + 7) / 8, render_y = (render_height_ + 7) / 8;
        const UINT output_x = (output_width_ + 7) / 8, output_y = (output_height_ + 7) / 8;

        pass("scene_fsr_luminance");
        run(context, programs_[luminance].Get(), {frame.color},
            {spd_counter_.uav.Get(), luminance_mip4_.Get(), luminance_mip5_.Get(), auto_exposure_.uav.Get()}, groups_x, groups_y);
        pass("scene_fsr_reconstruct");
        run(context, programs_[reconstruct].Get(), {frame.motion, frame.depth, frame.color, default_exposure_.srv.Get()},
            {previous_depth_.uav.Get(), motion_now.uav.Get(), dilated_depth_.uav.Get(), lock_luma_.uav.Get()}, render_x, render_y);
        pass("scene_fsr_depth_clip");
        run(context, programs_[depth_clip].Get(),
            {previous_depth_.srv.Get(), motion_now.srv.Get(), dilated_depth_.srv.Get(), default_reactive_.srv.Get(),
             default_reactive_.srv.Get(), motion_before.srv.Get(), frame.motion, frame.color, frame.depth,
             default_exposure_.srv.Get()},
            {reactive_masks_.uav.Get(), prepared_color_.uav.Get()}, render_x, render_y);
        pass("scene_fsr_lock");
        run(context, programs_[lock].Get(), {lock_luma_.srv.Get()}, {new_locks_.uav.Get(), previous_depth_.uav.Get()},
            render_x, render_y);
        pass("scene_fsr_accumulate");
        run(context, programs_[sharpen ? accumulate_sharpen : accumulate].Get(),
            {default_exposure_.srv.Get(), reactive_masks_.srv.Get(), motion_now.srv.Get(), upscaled_read.srv.Get(),
             lock_read.srv.Get(), prepared_color_.srv.Get(), lanczos_lut_.srv.Get(), maximum_bias_lut_.srv.Get(),
             luminance_.srv.Get(), auto_exposure_.srv.Get(), history_read.srv.Get()},
            {upscaled_write.uav.Get(), lock_write.uav.Get(), frame.output, new_locks_.uav.Get(), history_write.uav.Get()},
            output_x, output_y);
        if (sharpen) {
            pass("scene_fsr_rcas");
            ID3D11Buffer* rcas_buffers[2] = {constants_.Get(), rcas_constants_.Get()};
            context->CSSetConstantBuffers(0, 2, rcas_buffers);
            run(context, programs_[rcas].Get(), {default_exposure_.srv.Get(), upscaled_write.srv.Get()}, {frame.output},
                (output_width_ + 15) / 16, (output_height_ + 15) / 16);
        }
        ID3D11Buffer* no_buffers[2]{};
        context->CSSetConstantBuffers(0, 2, no_buffers);
        context->CSSetShader(nullptr, nullptr, 0);
        frame_ = (frame_ + 1) & 1u;
    }

private:
    static void write(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const void* data, std::size_t size) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, data, size);
            context->Unmap(buffer, 0);
        }
    }
    // One dispatch with the views in register order t0.., u0.. (the upstream
    // pass's FSR2_BIND_* numbering; a slot the program does not read is harmless).
    static void run(ID3D11DeviceContext* context, ID3D11ComputeShader* program,
                    std::initializer_list<ID3D11ShaderResourceView*> srvs,
                    std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT x, UINT y) {
        ID3D11ShaderResourceView* views[16]{};
        ID3D11UnorderedAccessView* unordered[8]{};
        UINT srv_count = 0, uav_count = 0;
        for (auto* view : srvs) views[srv_count++] = view;
        for (auto* view : uavs) unordered[uav_count++] = view;
        context->CSSetShader(program, nullptr, 0);
        context->CSSetShaderResources(0, srv_count, views);
        context->CSSetUnorderedAccessViews(0, uav_count, unordered, nullptr);
        context->Dispatch(x, y, 1);
        ID3D11ShaderResourceView* no_views[16]{};
        ID3D11UnorderedAccessView* no_unordered[8]{};
        context->CSSetShaderResources(0, srv_count, no_views);
        context->CSSetUnorderedAccessViews(0, uav_count, no_unordered, nullptr);
    }

    void clear_all(ID3D11DeviceContext* context) {
        const float zero[4]{};
        const UINT zero_uint[4]{};
        for (Image* image : {&prepared_color_, &dilated_motion_[0], &dilated_motion_[1], &dilated_depth_, &lock_luma_,
                             &reactive_masks_, &lock_status_[0], &lock_status_[1], &upscaled_[0], &upscaled_[1],
                             &luma_history_[0], &luma_history_[1], &new_locks_, &auto_exposure_})
            context->ClearUnorderedAccessViewFloat(image->uav.Get(), zero);
        context->ClearUnorderedAccessViewFloat(luminance_mip4_.Get(), zero);
        context->ClearUnorderedAccessViewFloat(luminance_mip5_.Get(), zero);
        context->ClearUnorderedAccessViewUint(previous_depth_.uav.Get(), zero_uint);
        context->ClearUnorderedAccessViewUint(spd_counter_.uav.Get(), zero_uint);
    }

    HRESULT image(ID3D11Device* device, Image& out, DXGI_FORMAT format, UINT width, UINT height, bool unordered,
                  UINT mips = 1, const void* data = nullptr, UINT pitch = 0) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = width; d.Height = height; d.MipLevels = mips; d.ArraySize = 1; d.Format = format;
        d.SampleDesc.Count = 1;
        d.Usage = data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unordered ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        const D3D11_SUBRESOURCE_DATA initial{data, pitch, 0};
        HRESULT hr = device->CreateTexture2D(&d, data ? &initial : nullptr, &out.texture);
        if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(out.texture.Get(), nullptr, &out.srv);
        if (SUCCEEDED(hr) && unordered) hr = device->CreateUnorderedAccessView(out.texture.Get(), nullptr, &out.uav);
        if (SUCCEEDED(hr)) {
            out.texture->GetDesc(&d);
            static const auto bytes = [](DXGI_FORMAT f) -> std::uint64_t {
                switch (f) {
                case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
                case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R16G16_FLOAT:
                case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
                case DXGI_FORMAT_R32G32_FLOAT: return 8;
                case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R8G8_UNORM: return 2;
                default: return 1;
                }
            };
            for (UINT mip = 0; mip < d.MipLevels; ++mip)
                storage_bytes_ += std::uint64_t(std::max(1u, width >> mip)) * std::max(1u, height >> mip) * bytes(format);
        }
        return hr;
    }

    HRESULT create_resources(ID3D11Device* device, UINT rw, UINT rh, UINT ow, UINT oh) {
        if (!device || !rw || !rh || !ow || !oh) return E_INVALIDARG;
        HRESULT hr;
        std::vector<ShaderProgram> list;
        shader_programs(list);
        for (int i = 0; i < program_count; ++i) {
            Ptr<ID3DBlob> code, errors;
            if (FAILED(hr = compile_shader(list[i], &code, &errors))) {
                if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                return hr;
            }
            if (FAILED(hr = create_shader(device, code->GetBufferPointer(), code->GetBufferSize(), nullptr, &programs_[i])))
                return hr;
        }
        D3D11_BUFFER_DESC b{};
        b.Usage = D3D11_USAGE_DYNAMIC; b.BindFlags = D3D11_BIND_CONSTANT_BUFFER; b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        b.ByteWidth = 128;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &constants_))) return hr;
        b.ByteWidth = 32;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &spd_constants_))) return hr;
        b.ByteWidth = 16;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &rcas_constants_))) return hr;
        D3D11_SAMPLER_DESC s{};
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.ComparisonFunc = D3D11_COMPARISON_NEVER; s.MaxLOD = D3D11_FLOAT32_MAX;
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (FAILED(hr = device->CreateSamplerState(&s, &point_))) return hr;
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        if (FAILED(hr = device->CreateSamplerState(&s, &linear_))) return hr;

        if (FAILED(hr = image(device, prepared_color_, DXGI_FORMAT_R16G16B16A16_FLOAT, rw, rh, true))) return hr;
        if (FAILED(hr = image(device, previous_depth_, DXGI_FORMAT_R32_UINT, rw, rh, true))) return hr;
        for (Image& motion : dilated_motion_)
            if (FAILED(hr = image(device, motion, DXGI_FORMAT_R16G16_FLOAT, rw, rh, true))) return hr;
        if (FAILED(hr = image(device, dilated_depth_, DXGI_FORMAT_R32_FLOAT, rw, rh, true))) return hr;
        if (FAILED(hr = image(device, lock_luma_, DXGI_FORMAT_R16_FLOAT, rw, rh, true))) return hr;
        if (FAILED(hr = image(device, reactive_masks_, DXGI_FORMAT_R8G8_UNORM, rw, rh, true))) return hr;
        const UINT lw = std::max(1u, rw / 2), lh = std::max(1u, rh / 2);
        if (FAILED(hr = image(device, luminance_, DXGI_FORMAT_R16_FLOAT, lw, lh, true, 0))) return hr;
        {
            D3D11_TEXTURE2D_DESC d{};
            luminance_.texture->GetDesc(&d);
            if (d.MipLevels <= 5) return E_INVALIDARG;  // the render size is too small for the shading-change mips
            D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = DXGI_FORMAT_R16_FLOAT; u.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            u.Texture2D.MipSlice = kLuminanceMipShadingChange;
            luminance_.uav.Reset();
            if (FAILED(hr = device->CreateUnorderedAccessView(luminance_.texture.Get(), &u, &luminance_mip4_))) return hr;
            u.Texture2D.MipSlice = 5;
            if (FAILED(hr = device->CreateUnorderedAccessView(luminance_.texture.Get(), &u, &luminance_mip5_))) return hr;
        }
        for (Image& status : lock_status_)
            if (FAILED(hr = image(device, status, DXGI_FORMAT_R16G16_FLOAT, ow, oh, true))) return hr;
        for (Image& color : upscaled_)
            if (FAILED(hr = image(device, color, DXGI_FORMAT_R16G16B16A16_FLOAT, ow, oh, true))) return hr;
        for (Image& history : luma_history_)
            if (FAILED(hr = image(device, history, DXGI_FORMAT_R8G8B8A8_UNORM, ow, oh, true))) return hr;
        if (FAILED(hr = image(device, new_locks_, DXGI_FORMAT_R8_UNORM, ow, oh, true))) return hr;
        if (FAILED(hr = image(device, spd_counter_, DXGI_FORMAT_R32_UINT, 1, 1, true))) return hr;
        if (FAILED(hr = image(device, auto_exposure_, DXGI_FORMAT_R32G32_FLOAT, 1, 1, true))) return hr;
        const float exposure[2]{};
        if (FAILED(hr = image(device, default_exposure_, DXGI_FORMAT_R32G32_FLOAT, 1, 1, false, 1, exposure, 8))) return hr;
        const std::uint8_t reactive = 0;
        if (FAILED(hr = image(device, default_reactive_, DXGI_FORMAT_R8_UNORM, 1, 1, false, 1, &reactive, 1))) return hr;
        std::int16_t lanczos[128];
        lanczos_table(lanczos);
        if (FAILED(hr = image(device, lanczos_lut_, DXGI_FORMAT_R16_SNORM, 128, 1, false, 1, lanczos, sizeof(lanczos)))) return hr;
        std::int16_t bias[kMaximumBiasTexels];
        maximum_bias_table(bias);
        if (FAILED(hr = image(device, maximum_bias_lut_, DXGI_FORMAT_R16_SNORM, FFX_FSR2_MAXIMUM_BIAS_TEXTURE_WIDTH,
                              FFX_FSR2_MAXIMUM_BIAS_TEXTURE_HEIGHT, false, 1, bias,
                              FFX_FSR2_MAXIMUM_BIAS_TEXTURE_WIDTH * sizeof(std::int16_t))))
            return hr;
        render_width_ = rw; render_height_ = rh; output_width_ = ow; output_height_ = oh;
        return S_OK;
    }
    friend class UpscaleSharpen;
    friend class Fsr2UpscalerD3D12;
    friend class UpscaleSharpenD3D12;
};

// The player's one Sharpening option after an upscaler that does not
// sharpen itself (DLSS: NVIDIA deprecated its sharpness parameter, so none
// is passed to NGX). FSR 2's RCAS pass (the same program FSR runs) over the
// upscaled output-size image, in place: the image is copied to a scratch
// texture of its size and format, and RCAS reads the copy and writes every
// pixel of the image (edges clamped, Fsr2Upscaler::rcas_source). One copy
// and one compute dispatch at output size; GPU-only resources, counted in
// the screen memory.
class UpscaleSharpen {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> program_;
    Ptr<ID3D11Buffer> constants_, rcas_constants_;
    Ptr<ID3D11Texture2D> exposure_, copy_;
    Ptr<ID3D11ShaderResourceView> exposure_srv_, copy_srv_;
    UINT width_ = 0, height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    float written_sharpness_ = -1.f;

    HRESULT create(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& image) {
        reset();
        HRESULT hr;
        Ptr<ID3DBlob> code, errors;
        if (FAILED(hr = compile_shader(Fsr2Upscaler::rcas_program(), &code, &errors))) {
            if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            return hr;
        }
        if (FAILED(hr = create_shader(device, code->GetBufferPointer(), code->GetBufferSize(), nullptr, &program_))) return hr;
        // cbFSR2: RCAS reads the display size (its edge clamp) and the pre-exposure (1).
        Fsr2Upscaler::Constants c{};
        c.display_size[0] = std::int32_t(image.Width);
        c.display_size[1] = std::int32_t(image.Height);
        c.pre_exposure = c.previous_pre_exposure = 1.f;
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = 128; b.Usage = D3D11_USAGE_IMMUTABLE; b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        unsigned char initial[128]{};
        std::memcpy(initial, &c, sizeof(c));
        const D3D11_SUBRESOURCE_DATA constants{initial, 0, 0};
        if (FAILED(hr = device->CreateBuffer(&b, &constants, &constants_))) return hr;
        b.ByteWidth = 16; b.Usage = D3D11_USAGE_DYNAMIC; b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &rcas_constants_))) return hr;
        // 1x1 exposure 0, as FSR's default exposure (exposure 1).
        D3D11_TEXTURE2D_DESC d{};
        d.Width = d.Height = d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R32G32_FLOAT; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const float exposure[2]{};
        const D3D11_SUBRESOURCE_DATA exposure_data{exposure, 8, 0};
        if (FAILED(hr = device->CreateTexture2D(&d, &exposure_data, &exposure_))) return hr;
        if (FAILED(hr = device->CreateShaderResourceView(exposure_.Get(), nullptr, &exposure_srv_))) return hr;
        d = {};
        d.Width = image.Width; d.Height = image.Height; d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = image.Format; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(hr = device->CreateTexture2D(&d, nullptr, &copy_))) return hr;
        if (FAILED(hr = device->CreateShaderResourceView(copy_.Get(), nullptr, &copy_srv_))) return hr;
        width_ = image.Width; height_ = image.Height; format_ = image.Format;
        return S_OK;
    }

public:
    void reset() { *this = UpscaleSharpen{}; }
    std::uint64_t storage_bytes() const { return copy_ ? std::uint64_t(width_) * height_ * 4u : 0u; }

    // Sharpens `image` (its unordered-access view `image_uav`, a 4-byte
    // colour format) by `sharpness` 0..1; 0 does nothing. Leaves no compute
    // stage resource bound. A failure leaves the image unsharpened.
    HRESULT apply(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* image,
                  ID3D11UnorderedAccessView* image_uav, float sharpness) {
        if (!(sharpness > 0.f)) return S_FALSE;
        if (!device || !context || !image || !image_uav) return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC desc{};
        image->GetDesc(&desc);
        if (!program_ || desc.Width != width_ || desc.Height != height_ || desc.Format != format_) {
            if (const HRESULT hr = create(device, desc); FAILED(hr)) { reset(); return hr; }
        }
        if (sharpness != written_sharpness_) {
            std::uint32_t rcas[4];
            Fsr2Upscaler::rcas_constants(sharpness, rcas);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(rcas_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return E_FAIL;
            std::memcpy(mapped.pData, rcas, sizeof(rcas));
            context->Unmap(rcas_constants_.Get(), 0);
            written_sharpness_ = sharpness;
        }
        context->CopyResource(copy_.Get(), image);
        ID3D11Buffer* buffers[2] = {constants_.Get(), rcas_constants_.Get()};
        ID3D11ShaderResourceView* views[2] = {exposure_srv_.Get(), copy_srv_.Get()};
        context->CSSetShader(program_.Get(), nullptr, 0);
        context->CSSetConstantBuffers(0, 2, buffers);
        context->CSSetShaderResources(0, 2, views);
        context->CSSetUnorderedAccessViews(0, 1, &image_uav, nullptr);
        context->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);  // 16x16 pixels a group
        ID3D11Buffer* no_buffers[2]{};
        ID3D11ShaderResourceView* no_views[2]{};
        ID3D11UnorderedAccessView* no_uav = nullptr;
        context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
        context->CSSetShaderResources(0, 2, no_views);
        context->CSSetConstantBuffers(0, 2, no_buffers);
        context->CSSetShader(nullptr, nullptr, 0);
        return S_OK;
    }
};

} // namespace owned
