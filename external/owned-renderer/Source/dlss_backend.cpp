#include "dlss_backend.h"
#include "build_version.h"
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <shlobj.h>
#include <cstdio>
#include <cwchar>

namespace owned {
namespace {
// EIRebuild64 identity, generated for this engine; never the upstream host ID.
constexpr char kProjectId[] = "666beccc-e6e1-4a4e-b13e-5bb45c14de81";
bool valid_quality(DlssQuality q) noexcept {
    return q >= DlssQuality::performance && q <= DlssQuality::dlaa;
}
NVSDK_NGX_PerfQuality_Value ngx_quality(DlssQuality q) noexcept {
    switch (q) {
    case DlssQuality::performance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case DlssQuality::balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case DlssQuality::quality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case DlssQuality::ultra_performance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case DlssQuality::dlaa: return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
    return NVSDK_NGX_PerfQuality_Value_MaxQuality; // callers validate first
}
bool make_directory(const wchar_t* path) noexcept {
    if (CreateDirectoryW(path, nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
}

bool DlssBackend::fail(const char* operation, std::uint32_t result) noexcept {
    std::snprintf(error_, sizeof(error_), "%s (0x%08x)", operation, static_cast<unsigned>(result));
    if (log_) log_(error_);
    return false;
}

bool DlssBackend::initialize(ID3D11Device* device, Log log) noexcept {
    shutdown();
    log_ = log;
    const auto unavailable = [&](const char* operation, std::uint32_t result) {
        fail(operation, result);
        capability_.available = false;
        capability_.result = result;
        std::snprintf(capability_.reason, sizeof(capability_.reason), "%s", error_);
        cleanup(); // also handles GetCapabilityParameters/AllocateParameters failures
        return false;
    };
    if (!device) return unavailable("NGX requires a native D3D11 device", E_INVALIDARG);
    device_ = device; // retain until after Shutdown1, including partial init
    wchar_t folder[32768]{};
    const DWORD count = GetModuleFileNameW(nullptr, folder, ARRAYSIZE(folder));
    if (!count || count >= ARRAYSIZE(folder)) return unavailable("NGX executable path unavailable", E_FAIL);
    wchar_t* slash = std::wcsrchr(folder, L'\\');
    if (!slash) return unavailable("NGX executable directory unavailable", E_FAIL);
    *slash = 0;
    wchar_t local[MAX_PATH]{};
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local);
    if (FAILED(hr)) return unavailable("NGX writable data directory unavailable", hr);
    wchar_t data[MAX_PATH]{};
    if (swprintf_s(data, L"%s\\EIRebuild64", local) < 0 || !make_directory(data))
        return unavailable("Cannot create EIRebuild64 NGX data directory", E_FAIL);
    if (swprintf_s(data, L"%s\\EIRebuild64\\NGX", local) < 0 || !make_directory(data))
        return unavailable("Cannot create NGX data directory", E_FAIL);
    const wchar_t* paths[] = {folder};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    auto result = NVSDK_NGX_D3D11_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        kBuildCommit, data, device_.Get(), &info, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(result)) return unavailable("NGX initialization failed", result);
    initialized_ = true;
    result = NVSDK_NGX_D3D11_GetCapabilityParameters(&capabilities_);
    if (NVSDK_NGX_FAILED(result) || !capabilities_) return unavailable("NGX capability query failed", result);
    int available = 0, needs_driver = 0, feature_result = 0;
    capability_.needs_updated_driver_known = NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetI(
        capabilities_, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver));
    capability_.needs_updated_driver = needs_driver != 0;
    capability_.feature_init_result_known = NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetI(
        capabilities_, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &feature_result));
    capability_.feature_init_result = static_cast<std::uint32_t>(feature_result);
    result = NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (NVSDK_NGX_FAILED(result)) return unavailable("NGX Available query failed", result);
    if (!available) return unavailable(capability_.needs_updated_driver ? "DLSS requires a newer NVIDIA driver" :
        "DLSS unavailable on this GPU/driver", capability_.feature_init_result_known ?
        capability_.feature_init_result : static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_FeatureNotSupported));
    result = NVSDK_NGX_D3D11_AllocateParameters(&parameters_);
    if (NVSDK_NGX_FAILED(result) || !parameters_) return unavailable("NGX parameter allocation failed", result);
    capability_.available = true;
    capability_.result = static_cast<std::uint32_t>(result);
    capability_.reason[0] = error_[0] = 0;
    if (log_) log_("DLSS NGX backend available");
    return true;
}

bool DlssBackend::device_ready() noexcept {
    if (!capability_.available || !device_) return fail("DLSS backend unavailable", E_FAIL);
    const HRESULT hr = device_->GetDeviceRemovedReason();
    if (FAILED(hr)) { device_lost(hr); return false; }
    return true;
}

bool DlssBackend::optimal(unsigned out_w, unsigned out_h, DlssQuality quality,
                           unsigned& render_w, unsigned& render_h) noexcept {
    render_w = render_h = 0;
    if (!device_ready()) return false;
    if (!out_w || !out_h || !valid_quality(quality)) return fail("Invalid DLSS optimal settings request", E_INVALIDARG);
    if (quality == DlssQuality::dlaa) { render_w = out_w; render_h = out_h; error_[0] = 0; return true; }
    unsigned w = 0, h = 0, max_w = 0, max_h = 0, min_w = 0, min_h = 0;
    float sharpness = 0;
    const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(capabilities_, out_w, out_h, ngx_quality(quality),
        &w, &h, &max_w, &max_h, &min_w, &min_h, &sharpness);
    if (NVSDK_NGX_FAILED(result) || !w || !h || w > out_w || h > out_h)
        return fail("DLSS optimal settings failed", result);
    render_w = w; render_h = h; error_[0] = 0;
    return true;
}

bool DlssBackend::create_feature(ID3D11DeviceContext* context, const DlssShape& shape) noexcept {
    if (!device_ready()) return false;
    if (!context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || !valid_quality(shape.quality) ||
        !shape.render_width || !shape.render_height || !shape.output_width || !shape.output_height ||
        shape.render_width > shape.output_width || shape.render_height > shape.output_height ||
        (shape.quality == DlssQuality::dlaa && (shape.render_width != shape.output_width || shape.render_height != shape.output_height)))
        return fail("Invalid DLSS feature request", E_INVALIDARG);
    Microsoft::WRL::ComPtr<ID3D11Device> context_device;
    context->GetDevice(&context_device);
    if (context_device.Get() != device_.Get()) return fail("DLSS context belongs to another device", E_INVALIDARG);
    if (feature_ && shape == shape_) return true;
    release_feature();
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = shape.render_width;
    create.Feature.InHeight = shape.render_height;
    create.Feature.InTargetWidth = shape.output_width;
    create.Feature.InTargetHeight = shape.output_height;
    create.Feature.InPerfQualityValue = ngx_quality(shape.quality);
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
        (shape.hdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0) |
        (shape.depth_inverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
        (shape.auto_exposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0);
    const auto result = NGX_D3D11_CREATE_DLSS_EXT(context, &feature_, parameters_, &create);
    if (NVSDK_NGX_FAILED(result) || !feature_) {
        release_feature();
        return fail("DLSS feature creation failed", result);
    }
    shape_ = shape;
    error_[0] = 0;
    if (log_) log_("DLSS feature created");
    return true;
}

bool DlssBackend::evaluate(ID3D11DeviceContext* context, const DlssFrame& frame) noexcept {
    if (!frame.color || !frame.depth || !frame.motion || !frame.output)
        return fail("DLSS frame inputs are missing", static_cast<std::uint32_t>(E_INVALIDARG));
    const bool recreated = !feature_ || !(shape_ == frame.shape);
    if (!create_feature(context, frame.shape)) return false;
    // Same NGX contract as upstream Host/dlss.cpp, without wire records/IDs.
    NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = frame.color;
    eval.Feature.pInOutput = frame.output;
    eval.Feature.InSharpness = 0.f; // shared renderer RCAS runs afterwards
    eval.pInDepth = frame.depth;
    eval.pInMotionVectors = frame.motion;
    eval.pInExposureTexture = frame.exposure;
    eval.InJitterOffsetX = frame.jitter_x;
    eval.InJitterOffsetY = frame.jitter_y;
    eval.InRenderSubrectDimensions.Width = frame.shape.render_width;
    eval.InRenderSubrectDimensions.Height = frame.shape.render_height;
    eval.InReset = frame.reset || recreated;
    eval.InMVScaleX = frame.mv_scale_x;
    eval.InMVScaleY = frame.mv_scale_y;
    const auto result = NGX_D3D11_EVALUATE_DLSS_EXT(context, feature_, parameters_, &eval);
    if (NVSDK_NGX_FAILED(result)) {
        release_feature();
        return fail("DLSS evaluation failed", result);
    }
    error_[0] = 0;
    return true;
}

void DlssBackend::release_feature() noexcept {
    if (feature_) {
        const auto result = NVSDK_NGX_D3D11_ReleaseFeature(feature_);
        if (NVSDK_NGX_FAILED(result)) fail("DLSS feature release failed", result);
        feature_ = nullptr;
    }
    shape_ = {};
}
void DlssBackend::cleanup() noexcept {
    release_feature();
    if (parameters_) { NVSDK_NGX_D3D11_DestroyParameters(parameters_); parameters_ = nullptr; }
    if (capabilities_) { NVSDK_NGX_D3D11_DestroyParameters(capabilities_); capabilities_ = nullptr; }
    if (initialized_ && device_) {
        const auto result = NVSDK_NGX_D3D11_Shutdown1(device_.Get());
        if (NVSDK_NGX_FAILED(result) && log_) log_("NGX Shutdown1 failed");
    }
    initialized_ = false;
    device_.Reset();
}
void DlssBackend::shutdown() noexcept {
    cleanup();
    capability_ = {};
    error_[0] = 0;
    log_ = nullptr;
}
void DlssBackend::device_lost(HRESULT reason) noexcept {
    fail("DLSS device lost", static_cast<std::uint32_t>(reason));
    capability_.available = false;
    capability_.result = static_cast<std::uint32_t>(reason);
    std::snprintf(capability_.reason, sizeof(capability_.reason), "%s", error_);
    cleanup();
}
} // namespace owned
