// DLSS Super Resolution through NGX (NVIDIA DLSS SDK, linked from the SDK
// folder; nvngx_dlss.dll ships next to the host exe). Conventions from the
// DLSS Programming Guide: render-resolution motion vectors, current ->
// previous, in render pixels (MVLowRes); jitter in render pixels; NGX
// preserves the immediate D3D11 context's state. On the D3D12 backend NGX
// records into the executor's command list (d3d12_frame.h record_external).
#include "dlss.h"
#include "../Source/runtime_diagnostics.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <dxgiformat.h>
#include <d3d12.h>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstring>
#ifdef EID3D11_EMBEDDED
#include "../Source/engine_bridge.h"
#endif

namespace remote {
namespace {

#ifdef EID3D11_EMBEDDED
// NGX can call from its worker threads. The engine journal is asynchronous
// and thread-safe. Split messages below its 768-byte bridge detail limit.
void NVSDK_CONV ngx_log_callback(const char* message, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature source) {
    if (!message) return;
    while (*message) {
        char text[720];
        const std::size_t count = (std::min)(std::strlen(message), std::size_t(600));
        std::snprintf(text, sizeof(text), "NGX level=%u feature=%u %.*s", unsigned(level), unsigned(source), int(count), message);
        owned::bridge_report_event("d3d12_ngx_sdk", text);
        message += count;
    }
}
#endif

void log_fg_inputs(Dlss::Log log, NVSDK_NGX_Parameter* parameters, ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources,
                   const NVSDK_NGX_DLSSG_Opt_Eval_Params& opt) noexcept {
    if (!log) return;
    char text[720];
    const char* create_keys[] = {NVSDK_NGX_Parameter_Width, NVSDK_NGX_Parameter_Height,
        NVSDK_NGX_DLSSG_Parameter_InternalWidth, NVSDK_NGX_DLSSG_Parameter_InternalHeight,
        NVSDK_NGX_DLSSG_Parameter_BackbufferFormat, NVSDK_NGX_DLSSG_Parameter_DynamicResolution,
        NVSDK_NGX_Parameter_CreationNodeMask, NVSDK_NGX_Parameter_VisibilityNodeMask};
    for (const char* key : create_keys) {
        unsigned value = 0;
        const NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetUI(parameters, key, &value);
        std::snprintf(text, sizeof(text), "DLSSG parameter %s=%u (get=0x%08x)", key, value, unsigned(result));
        log(text);
    }
    std::snprintf(text, sizeof(text), "DLSSG list=%p type=%u multiFrameCount=%u multiFrameIndex=%u jitter=(%g,%g) mvecScale=(%g,%g) reset=%u HDR=%u depthInverted=%u cameraMotionIncluded=%u notRenderingGameFrames=%u",
                  static_cast<void*>(list), unsigned(list->GetType()), opt.multiFrameCount, opt.multiFrameIndex,
                  opt.jitterOffset[0], opt.jitterOffset[1], opt.mvecScale[0], opt.mvecScale[1], unsigned(opt.reset),
                  unsigned(opt.colorBuffersHDR), unsigned(opt.depthInverted), unsigned(opt.cameraMotionIncluded), unsigned(opt.notRenderingGameFrames));
    log(text);
    constexpr unsigned shader_read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const unsigned states[] = {shader_read, shader_read | D3D12_RESOURCE_STATE_DEPTH_READ, shader_read, shader_read, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    const char* names[] = {"Backbuffer", "Depth", "MVecs", "HUDLess", "OutputInterpolated"};
    for (unsigned i = 0; i < 5; ++i) {
        const D3D12_RESOURCE_DESC desc = resources[i] ? resources[i]->GetDesc() : D3D12_RESOURCE_DESC{};
        // record_external transitions to these states before entering NGX.
        std::snprintf(text, sizeof(text), "DLSSG %s=%p format=%u size=%llux%u dimension=%u flags=0x%x samples=%u stateAtEntry=0x%x",
                      names[i], static_cast<void*>(resources[i]), unsigned(desc.Format), static_cast<unsigned long long>(desc.Width), desc.Height,
                      unsigned(desc.Dimension), unsigned(desc.Flags), desc.SampleDesc.Count, states[i]);
        log(text);
    }
    log("DLSSG UI/UIAlpha/BidirectionalDistortionField/OutputReal/OutputDisableInterpolation=null; all subrect bases/sizes=0; pinhole=(0,0); minRelativeLinearDepthObjectSeparation=0");
    std::snprintf(text, sizeof(text), "DLSSG camera near=%g far=%g fov=%g aspect=%g pos=(%g,%g,%g) up=(%g,%g,%g) right=(%g,%g,%g) fwd=(%g,%g,%g)",
                  opt.cameraNear, opt.cameraFar, opt.cameraFOV, opt.cameraAspectRatio,
                  opt.cameraPos[0], opt.cameraPos[1], opt.cameraPos[2], opt.cameraUp[0], opt.cameraUp[1], opt.cameraUp[2],
                  opt.cameraRight[0], opt.cameraRight[1], opt.cameraRight[2], opt.cameraFwd[0], opt.cameraFwd[1], opt.cameraFwd[2]);
    log(text);
    const float (*matrices[])[4] = {opt.cameraViewToClip, opt.clipToCameraView, opt.clipToLensClip, opt.clipToPrevClip, opt.prevClipToClip};
    const char* matrix_names[] = {"CameraViewToClip", "ClipToCameraView", "ClipToLensClip", "ClipToPrevClip", "PrevClipToClip"};
    for (unsigned i = 0; i < 5; ++i) {
        for (unsigned row = 0; row < 4; ++row) {
            const float* m = matrices[i][row];
            std::snprintf(text, sizeof(text), "DLSSG %s[%u]=(%g,%g,%g,%g)", matrix_names[i], row, m[0], m[1], m[2], m[3]);
            log(text);
        }
    }
}
// Our fixed NGX project id (custom engine).
#ifdef EID3D11_EMBEDDED
constexpr char kProjectId[] = "666beccc-e6e1-4a4e-b13e-5bb45c14de81"; // EIRebuild64
#else
constexpr char kProjectId[] = "6d0f1b52-3c7e-4a9d-8e41-2b5f7c9a0d63";
#endif

NVSDK_NGX_PerfQuality_Value quality_of(int mode) noexcept {
    switch (mode) {
    case 0: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case 1: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case 2: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case 3: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case 4: return NVSDK_NGX_PerfQuality_Value_DLAA;
    default: return NVSDK_NGX_PerfQuality_Value_Balanced;
    }
}

// The feature create parameters of a record (both APIs).
NVSDK_NGX_DLSS_Create_Params create_params(const x64remote::DlssEvaluateRecord& record, int flags) noexcept {
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = record.render_w;
    create.Feature.InHeight = record.render_h;
    create.Feature.InTargetWidth = record.out_w;
    create.Feature.InTargetHeight = record.out_h;
    create.Feature.InPerfQualityValue = quality_of(record.mode);
    create.InFeatureCreateFlags = flags;
    return create;
}

} // namespace

void Dlss::initialize_owned(ID3D11Device* device, const std::wstring& folder, x64remote::Capabilities& caps, Log log) noexcept {
    device_ = device;
    log_ = log;
    folder_ = folder;
    paths_[0] = folder_.c_str();
    info_ = {};
    info_.PathListInfo.Path = paths_;
    info_.PathListInfo.Length = 1;
#ifdef EID3D11_EMBEDDED
    info_.LoggingInfo.LoggingCallback = RuntimeDiagnostics::Enabled() ? &ngx_log_callback : nullptr;
    info_.LoggingInfo.MinimumLoggingLevel = RuntimeDiagnostics::Enabled() ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_OFF;
    info_.LoggingInfo.DisableOtherLoggingSinks = !RuntimeDiagnostics::Enabled();
#endif
    const NVSDK_NGX_Result result = NVSDK_NGX_D3D11_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
                                                                        folder_.c_str(), device, &info_, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(result)) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "NGX init failed (NVSDK_NGX_Result 0x%08x)", static_cast<unsigned>(result));
        caps.dlss_available = 0;
        char line[200];
        std::snprintf(line, sizeof(line), "DLSS unavailable: %s", caps.dlss_reason);
        log_(line);
        return;
    }
    initialized_ = true;
    query_capabilities(caps);
}

void Dlss::initialize_d3d12_owned(ID3D12Device* device, x64remote::Capabilities& caps) noexcept {
    shutdown_owned();  // the D3D11 instance (nothing when its init failed)
    device_ = nullptr;
    device12_ = device;
    if (folder_.empty()) {  // initialize never ran: nowhere to find nvngx_dlss.dll
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "%s", "NGX was not initialised");
        caps.dlss_available = 0;
        return;
    }
    info_ = {};
    paths_[0] = folder_.c_str();
    info_.PathListInfo.Path = paths_;
    info_.PathListInfo.Length = 1;
#ifdef EID3D11_EMBEDDED
    info_.LoggingInfo.LoggingCallback = RuntimeDiagnostics::Enabled() ? &ngx_log_callback : nullptr;
    info_.LoggingInfo.MinimumLoggingLevel = RuntimeDiagnostics::Enabled() ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_OFF;
    info_.LoggingInfo.DisableOtherLoggingSinks = !RuntimeDiagnostics::Enabled();
    if (RuntimeDiagnostics::Enabled() && log_) log_("NGX D3D12 verbose logging enabled in Logs64 (SDK callback also in engine journal)");
#endif
    const NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
#ifdef EID3D11_EMBEDDED
                                                                        data_folder_.c_str(), device, &info_, NVSDK_NGX_Version_API);
#else
                                                                        folder_.c_str(), device, &info_, NVSDK_NGX_Version_API);
#endif
    if (NVSDK_NGX_FAILED(result)) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "NGX init on Direct3D 12 failed (NVSDK_NGX_Result 0x%08x)",
                      static_cast<unsigned>(result));
        caps.dlss_available = 0;
        if (log_) { char line[200]; std::snprintf(line, sizeof(line), "DLSS unavailable: %s", caps.dlss_reason); log_(line); }
        return;
    }
    initialized_ = true;
    query_capabilities(caps);
    query_frame_generation();
}

void Dlss::query_capabilities(x64remote::Capabilities& caps) noexcept {
    char text[200];
    const auto unavailable = [&](const char* reason) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "%s", reason);
        caps.dlss_available = 0;
        char line[260];
        std::snprintf(line, sizeof(line), "DLSS unavailable: %s", reason);
        if (log_) log_(line);
    };
    NVSDK_NGX_Result result = device12_ ? NVSDK_NGX_D3D12_GetCapabilityParameters(&capabilities_)
                                        : NVSDK_NGX_D3D11_GetCapabilityParameters(&capabilities_);
    if (NVSDK_NGX_FAILED(result) || !capabilities_) {
        std::snprintf(text, sizeof(text), "NGX capability query failed (0x%08x)", static_cast<unsigned>(result));
        unavailable(text);
        return;
    }
    int available = 0, needs_driver = 0, init_result = 0;
    NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!available) {
        NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
        NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
        std::snprintf(text, sizeof(text), "DLSS not available on this GPU/driver%s (feature init 0x%08x)",
                      needs_driver ? ", a newer driver is needed" : "", static_cast<unsigned>(init_result));
        unavailable(text);
        return;
    }
    result = device12_ ? NVSDK_NGX_D3D12_AllocateParameters(&parameters_) : NVSDK_NGX_D3D11_AllocateParameters(&parameters_);
    if (NVSDK_NGX_FAILED(result) || !parameters_) {
        std::snprintf(text, sizeof(text), "NGX parameter allocation failed (0x%08x)", static_cast<unsigned>(result));
        unavailable(text);
        return;
    }
    available_ = true;
    caps.dlss_available = 1;
    caps.dlss_reason[0] = 0;
    if (log_) log_(device12_ ? "DLSS Super Resolution available (NGX initialised on Direct3D 12)" : "DLSS Super Resolution available (NGX initialised)");
}

bool Dlss::optimal_owned(unsigned out_w, unsigned out_h, int mode, unsigned& render_w, unsigned& render_h) noexcept {
    render_w = render_h = 0;
    if (!available_ || !out_w || !out_h) return false;
    if (mode == 4) { render_w = out_w; render_h = out_h; return true; } // DLAA renders at output size
    unsigned max_w = 0, max_h = 0, min_w = 0, min_h = 0;
    float sharpness = 0;
    const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(capabilities_, out_w, out_h, quality_of(mode), &render_w,
                                                                  &render_h, &max_w, &max_h, &min_w, &min_h, &sharpness);
    if (NVSDK_NGX_FAILED(result) || !render_w || !render_h) {
        char text[160];
        std::snprintf(text, sizeof(text), "DLSS optimal settings %ux%u mode %d failed (0x%08x)", out_w, out_h, mode,
                      static_cast<unsigned>(result));
        log_(text);
        return false;
    }
    return true;
}

// The feature shape of a record (recreated when it changes).
static int feature_flags(const x64remote::DlssEvaluateRecord& record, bool auto_exposure) noexcept {
    return NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
           ((record.flags & x64remote::dlss_hdr) ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0) |
           ((record.flags & x64remote::dlss_depth_inverted) ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
           (auto_exposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0);
}

bool Dlss::evaluate_owned(ID3D11DeviceContext* context, const x64remote::DlssEvaluateRecord& record, ID3D11Resource* color,
                    ID3D11Resource* depth, ID3D11Resource* motion, ID3D11Resource* output, ID3D11Resource* exposure,
                    char* error, std::size_t error_bytes) noexcept {
    if (!available_ || device12_) { std::snprintf(error, error_bytes, "DLSS evaluate without DLSS available on Direct3D 11"); return false; }
    if (!color || !depth || !motion || !output) {
        std::snprintf(error, error_bytes, "DLSS evaluate with a missing resource (color %p depth %p motion %p output %p)",
                      static_cast<void*>(color), static_cast<void*>(depth), static_cast<void*>(motion), static_cast<void*>(output));
        return false;
    }
    Shape shape;
    shape.render_w = record.render_w;
    shape.render_h = record.render_h;
    shape.out_w = record.out_w;
    shape.out_h = record.out_h;
    shape.mode = record.mode;
    shape.flags = feature_flags(record, !exposure);
    if (!feature_ || !(shape == shape_)) {
        release_feature();
        NVSDK_NGX_DLSS_Create_Params create = create_params(record, shape.flags);
        const NVSDK_NGX_Result result = NGX_D3D11_CREATE_DLSS_EXT(context, &feature_, parameters_, &create);
        char text[200];
        std::snprintf(text, sizeof(text), "DLSS feature %s: render %ux%u -> output %ux%u mode %d flags 0x%x: 0x%08x",
                      shape_.mode < 0 ? "created" : "recreated", shape.render_w, shape.render_h, shape.out_w, shape.out_h,
                      shape.mode, static_cast<unsigned>(shape.flags), static_cast<unsigned>(result));
        log_(text);
        if (NVSDK_NGX_FAILED(result) || !feature_) {
            feature_ = nullptr;
            std::snprintf(error, error_bytes, "DLSS feature creation failed (NVSDK_NGX_Result 0x%08x)", static_cast<unsigned>(result));
            return false;
        }
        shape_ = shape;
    }
    NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = color;
    eval.Feature.pInOutput = output;
    eval.Feature.InSharpness = record.sharpness;
    eval.pInDepth = depth;
    eval.pInMotionVectors = motion;
    eval.pInExposureTexture = exposure;
    eval.InJitterOffsetX = record.jitter_x;
    eval.InJitterOffsetY = record.jitter_y;
    eval.InRenderSubrectDimensions.Width = record.render_w;
    eval.InRenderSubrectDimensions.Height = record.render_h;
    eval.InReset = (record.flags & x64remote::dlss_reset) ? 1 : 0;
    eval.InMVScaleX = record.mv_scale_x != 0.f ? record.mv_scale_x : 1.f;
    eval.InMVScaleY = record.mv_scale_y != 0.f ? record.mv_scale_y : 1.f;
    const NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_DLSS_EXT(context, feature_, parameters_, &eval);
    if (NVSDK_NGX_FAILED(result)) {
        std::snprintf(error, error_bytes, "DLSS evaluate failed (NVSDK_NGX_Result 0x%08x)", static_cast<unsigned>(result));
        return false;
    }
    return true;
}

// The D3D12 form of evaluate_owned, recorded into the executor's list. No
// exposure texture: auto exposure, as the renderer passes none on D3D11.
bool Dlss::evaluate_d3d12_owned(ID3D12GraphicsCommandList* list, const x64remote::DlssEvaluateRecord& record,
                                ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept {
    if (!available_ || !device12_) { std::snprintf(error, error_bytes, "DLSS evaluate without DLSS available on Direct3D 12"); return false; }
    if (!list || !resources || !resources[0] || !resources[1] || !resources[2] || !resources[3]) {
        std::snprintf(error, error_bytes, "DLSS evaluate with a missing list or resource");
        return false;
    }
    Shape shape;
    shape.render_w = record.render_w;
    shape.render_h = record.render_h;
    shape.out_w = record.out_w;
    shape.out_h = record.out_h;
    shape.mode = record.mode;
    shape.flags = feature_flags(record, true);
    if (!feature_ || !(shape == shape_)) {
        release_feature();
        NVSDK_NGX_DLSS_Create_Params create = create_params(record, shape.flags);
        const NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(list, 1, 1, &feature_, parameters_, &create);
        char text[200];
        std::snprintf(text, sizeof(text), "DLSS feature (Direct3D 12) %s: render %ux%u -> output %ux%u mode %d flags 0x%x: 0x%08x",
                      shape_.mode < 0 ? "created" : "recreated", shape.render_w, shape.render_h, shape.out_w, shape.out_h,
                      shape.mode, static_cast<unsigned>(shape.flags), static_cast<unsigned>(result));
        log_(text);
        if (NVSDK_NGX_FAILED(result) || !feature_) {
            feature_ = nullptr;
            std::snprintf(error, error_bytes, "DLSS feature creation failed (NVSDK_NGX_Result 0x%08x)", static_cast<unsigned>(result));
            return false;
        }
        shape_ = shape;
    }
    NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = resources[0];
    eval.Feature.pInOutput = resources[3];
    eval.Feature.InSharpness = record.sharpness;
    eval.pInDepth = resources[1];
    eval.pInMotionVectors = resources[2];
    eval.InJitterOffsetX = record.jitter_x;
    eval.InJitterOffsetY = record.jitter_y;
    eval.InRenderSubrectDimensions.Width = record.render_w;
    eval.InRenderSubrectDimensions.Height = record.render_h;
    eval.InReset = (record.flags & x64remote::dlss_reset) ? 1 : 0;
    eval.InMVScaleX = record.mv_scale_x != 0.f ? record.mv_scale_x : 1.f;
    eval.InMVScaleY = record.mv_scale_y != 0.f ? record.mv_scale_y : 1.f;
    const NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(list, feature_, parameters_, &eval);
    if (NVSDK_NGX_FAILED(result)) {
        std::snprintf(error, error_bytes, "DLSS evaluate failed (NVSDK_NGX_Result 0x%08x)", static_cast<unsigned>(result));
        return false;
    }
    return true;
}

void Dlss::release_feature() noexcept {
    if (!feature_) return;
    if (device12_) {
        if (wait_idle_) wait_idle_();  // frames already submitted may still run the feature
        NVSDK_NGX_D3D12_ReleaseFeature(feature_);
    } else {
        NVSDK_NGX_D3D11_ReleaseFeature(feature_);
    }
    feature_ = nullptr;
}

void Dlss::release_owned() noexcept {
    if (!feature_) return;
    release_feature();
    shape_ = Shape{};
    if (log_) log_("DLSS feature released (DLSS off)");
}

void Dlss::shutdown_owned() noexcept {
    release_frame_generation_owned();
    fg_available_ = false;
    release_feature();
    shape_ = Shape{};
    if (device12_) {
        if (parameters_) { NVSDK_NGX_D3D12_DestroyParameters(parameters_); parameters_ = nullptr; }
        if (capabilities_) { NVSDK_NGX_D3D12_DestroyParameters(capabilities_); capabilities_ = nullptr; }
        if (initialized_) NVSDK_NGX_D3D12_Shutdown1(device12_);
    } else {
        if (parameters_) { NVSDK_NGX_D3D11_DestroyParameters(parameters_); parameters_ = nullptr; }
        if (capabilities_) { NVSDK_NGX_D3D11_DestroyParameters(capabilities_); capabilities_ = nullptr; }
        if (initialized_ && device_) NVSDK_NGX_D3D11_Shutdown1(device_);
    }
    initialized_ = available_ = false;
#ifdef EID3D11_EMBEDDED
    device_ = nullptr;
    device12_ = nullptr;
    fg_multi_frame_max_ = 0;
    std::snprintf(fg_reason_, sizeof(fg_reason_), "NGX was not initialised on Direct3D 12");
#endif
}

// ---- DLSS Frame Generation (D3D12) -----------------------------------------
// DLSS-FG Programming Guide 310.7.0: inputs in NON_PIXEL_SHADER_RESOURCE
// (the executor's shader-read state includes it), the interpolated output in
// UNORDERED_ACCESS, the same size and format as the backbuffer; the feature
// neither paces nor presents (section 7: the application does).
void Dlss::query_frame_generation() noexcept {
    fg_available_ = false;
    fg_multi_frame_max_ = 0;
    if (!device12_ || !capabilities_ || !parameters_) {
        std::snprintf(fg_reason_, sizeof(fg_reason_), "%s", "DLSS is not available (NGX)");
        return;
    }
    int available = 0, needs_driver = 0, init_result = 0;
    unsigned multi = 0;
    NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_FrameGeneration_Available, &available);
    if (!available) {
        NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver, &needs_driver);
        NVSDK_NGX_Parameter_GetI(capabilities_, NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &init_result);
        std::snprintf(fg_reason_, sizeof(fg_reason_),
                      "DLSS Frame Generation not available on this GPU/driver%s (feature init 0x%08x; needs a GeForce RTX 40 or 50)",
                      needs_driver ? ", a newer driver is needed" : "", static_cast<unsigned>(init_result));
    } else {
        if (NVSDK_NGX_FAILED(NVSDK_NGX_Parameter_GetUI(capabilities_, NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &multi))) multi = 1;
        fg_available_ = true;
        fg_multi_frame_max_ = static_cast<int>(multi);
        fg_reason_[0] = 0;
    }
    if (log_) {
        char line[260];
        if (fg_available_) std::snprintf(line, sizeof(line), "DLSS Frame Generation available (multi-frame count max %d)", fg_multi_frame_max_);
        else std::snprintf(line, sizeof(line), "DLSS Frame Generation unavailable: %s", fg_reason_);
        log_(line);
    }
}

bool Dlss::evaluate_frame_generation_owned(ID3D12GraphicsCommandList* list, const x64remote::DlssgEvaluate& record,
                                           ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept {
    if (!fg_available_ || !device12_) {
        std::snprintf(error, error_bytes, "DLSS Frame Generation is not available: %s", fg_reason_);
        return false;
    }
    if (!list || !resources || !resources[0] || !resources[1] || !resources[2] || !resources[4]) {
        std::snprintf(error, error_bytes, "DLSS Frame Generation evaluate with a missing list or resource");
        return false;
    }
    if (!fg_feature_ || fg_width_ != record.width || fg_height_ != record.height || fg_render_w_ != record.render_w ||
        fg_render_h_ != record.render_h) {
        release_frame_generation_owned();
        NVSDK_NGX_DLSSG_Create_Params create{};
        create.Width = record.width;
        create.Height = record.height;
        create.NativeBackbufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        create.RenderWidth = record.render_w;
        create.RenderHeight = record.render_h;
        create.DynamicResolutionScaling = false;
        const NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSSG(list, 1, 1, &fg_feature_, parameters_, &create);
        char text[200];
        std::snprintf(text, sizeof(text), "DLSS Frame Generation feature created: %ux%u (depth/motion %ux%u): 0x%08x", record.width,
                      record.height, record.render_w, record.render_h, static_cast<unsigned>(result));
        if (log_) log_(text);
        if (NVSDK_NGX_FAILED(result) || !fg_feature_) {
            fg_feature_ = nullptr;
            std::snprintf(error, error_bytes, "DLSS Frame Generation feature creation failed (NVSDK_NGX_Result 0x%08x)",
                          static_cast<unsigned>(result));
            return false;
        }
        fg_width_ = record.width;
        fg_height_ = record.height;
        fg_render_w_ = record.render_w;
        fg_render_h_ = record.render_h;
    }
    NVSDK_NGX_D3D12_DLSSG_Eval_Params eval{};
    eval.pBackbuffer = resources[0];
    eval.pDepth = resources[1];
    eval.pMVecs = resources[2];
    eval.pHudless = resources[3];
    eval.pOutputInterpFrame = resources[4];
    NVSDK_NGX_DLSSG_Opt_Eval_Params opt{};
    opt.multiFrameCount = 1;
    opt.multiFrameIndex = 1;
    std::memcpy(opt.cameraViewToClip, record.view_to_clip, sizeof(opt.cameraViewToClip));
    std::memcpy(opt.clipToCameraView, record.clip_to_view, sizeof(opt.clipToCameraView));
    std::memcpy(opt.clipToPrevClip, record.clip_to_prev_clip, sizeof(opt.clipToPrevClip));
    std::memcpy(opt.prevClipToClip, record.prev_clip_to_clip, sizeof(opt.prevClipToClip));
    for (int i = 0; i < 4; ++i) opt.clipToLensClip[i][i] = 1.f;  // no lens distortion
    opt.jitterOffset[0] = record.jitter_x;
    opt.jitterOffset[1] = record.jitter_y;
    opt.mvecScale[0] = record.mv_scale_x;
    opt.mvecScale[1] = record.mv_scale_y;
    std::memcpy(opt.cameraPos, record.position, sizeof(opt.cameraPos));
    std::memcpy(opt.cameraUp, record.up, sizeof(opt.cameraUp));
    std::memcpy(opt.cameraRight, record.right, sizeof(opt.cameraRight));
    std::memcpy(opt.cameraFwd, record.forward, sizeof(opt.cameraFwd));
    opt.cameraNear = record.near_plane;
    opt.cameraFar = record.far_plane;
    opt.cameraFOV = record.fov_y;
    opt.cameraAspectRatio = record.aspect;
    opt.colorBuffersHDR = false;
    opt.depthInverted = record.depth_inverted;
    opt.cameraMotionIncluded = true;     // the motion pass writes camera and object motion
    opt.reset = record.reset;
    opt.notRenderingGameFrames = false;  // such presents are not evaluated at all
    const NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSSG(list, fg_feature_, parameters_, &eval, &opt);
    if (NVSDK_NGX_FAILED(result)) {
        log_fg_inputs(log_, parameters_, list, resources, opt);
        std::snprintf(error, error_bytes, "DLSS Frame Generation evaluate failed (NVSDK_NGX_Result 0x%08x)", static_cast<unsigned>(result));
        return false;
    }
    return true;
}

void Dlss::release_frame_generation_owned() noexcept {
    if (!fg_feature_) return;
    if (wait_idle_) wait_idle_();  // frames already submitted may still run the feature
    NVSDK_NGX_D3D12_ReleaseFeature(fg_feature_);
    fg_feature_ = nullptr;
    fg_width_ = fg_height_ = fg_render_w_ = fg_render_h_ = 0;
    if (log_) log_("DLSS Frame Generation feature released");
}

bool Dlss::frame_generation_caps(x64remote::DlssgCaps& out) noexcept {
    out = {};
    auto job = [&] {
        out.available = fg_available_;
        out.multi_frame_max = fg_multi_frame_max_;
        std::snprintf(out.reason, sizeof(out.reason), "%s", fg_reason_);
    };
    if (!worker_.joinable() || !run(job)) std::snprintf(out.reason, sizeof(out.reason), "%s", "NGX is not initialised");
    return out.available;
}

bool Dlss::evaluate_frame_generation_d3d12(ID3D12GraphicsCommandList* list, const x64remote::DlssgEvaluate& record,
                                           ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept {
    bool result = false;
    auto job = [&] { result = evaluate_frame_generation_owned(list, record, resources, error, error_bytes); };
    if (!run(job)) std::snprintf(error, error_bytes, "DLSS unavailable: the NGX thread could not be started");
    return result;
}

void Dlss::release_frame_generation() noexcept {
    if (!worker_.joinable()) return;
    auto job = [&] { release_frame_generation_owned(); };
    run(job);
}

std::uint64_t Dlss::frame_generation_vram(unsigned width, unsigned height, unsigned render_w, unsigned render_h) noexcept {
    std::uint64_t bytes = 0;
    auto job = [&] {
        if (!fg_available_ || !capabilities_) return;
        std::size_t estimate = 0;
        if (NVSDK_NGX_SUCCEED(NGX_D3D12_ESTIMATE_VRAM_DLSSG(capabilities_, render_w, render_h, width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                                           DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM,
                                                           DXGI_FORMAT_UNKNOWN, &estimate)))
            bytes = estimate;
    };
    if (worker_.joinable()) run(job);
    return bytes;
}

// ---- The NGX thread --------------------------------------------------------
// Every NGX call (init, capability and optimal-size queries, feature create,
// evaluate, release, shutdown) runs on this one thread; the caller blocks
// until it has finished, so the immediate context is never used from two
// threads at once (nvsdk_ngx.h: "Methods in this library are NOT thread
// safe. It is up to the client to ensure that thread safety is enforced as
// needed."). The reason it is a separate thread: the NVIDIA D3D11 driver
// keeps a pointer derived from the sampler description NGX passes when NGX
// creates its own samplers (in feature creation and in evaluate), and on
// every later CreateSamplerState from any caller it stores its sampler
// counter at that address - 4 (RENDERER_ARCHITECTURE.md L-091). With NGX on
// the replay thread that address was a dead NGX frame of the replay thread's
// stack, and later renderer frames there were corrupted. On this thread it
// is this thread's own stack below its wait, where nothing else lives.
bool Dlss::run_raw(void (*job)(void*), void* context) noexcept {
    if (worker_failed_) return false; // NGX never runs anywhere else (see above)
    if (worker_.joinable() && GetCurrentThreadId() == worker_id_.load()) { job(context); return true; }
    if (!worker_.joinable()) {
        try {
            stop_ = false;
            worker_ = std::thread([this] {
                worker_id_.store(GetCurrentThreadId());
                std::unique_lock<std::mutex> lock(mutex_);
                for (;;) {
                    wake_.wait(lock, [this] { return job_ != nullptr || stop_; });
                    if (job_) {
                        if (device12_) {
                            // Let the D3D12 caller report a long NGX operation.
                            // Its job context stays alive until done_ is signalled.
                            const auto job = job_;
                            void* const context = job_context_;
                            lock.unlock();
                            job(context);
                            lock.lock();
                        } else {
                            job_(job_context_);
                        }
                        job_ = nullptr;
                        done_.notify_all();
                        continue;
                    }
                    return;
                }
            });
        } catch (...) {
            // No thread: DLSS is unavailable. NGX is never run on the caller's
            // thread (that is the crashing path).
            worker_failed_ = true;
            if (log_) log_("DLSS unavailable: the NGX thread could not be started");
            return false;
        }
    }
    std::unique_lock<std::mutex> lock(mutex_);
    const bool d3d12_job = device12_ != nullptr;
    const Log wait_log = log_;
    job_context_ = context;
    job_ = job;
    wake_.notify_one();
    if (d3d12_job) {
        while (!done_.wait_for(lock, std::chrono::seconds(20), [this] { return job_ == nullptr; })) {
            lock.unlock();
            if (wait_log) wait_log("D3D12 NGX operation still pending after 20 s; waiting before releasing its inputs");
            lock.lock();
        }
    } else {
        done_.wait(lock, [this] { return job_ == nullptr; });
    }
    return true;
}

void Dlss::stop_worker() noexcept {
    if (!worker_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    worker_.join();
    worker_id_.store(0);
}

void Dlss::initialize(ID3D11Device* device, const std::wstring& folder, x64remote::Capabilities& caps, Log log) noexcept {
    log_ = log;
    auto job = [&] { initialize_owned(device, folder, caps, log); };
    if (!run(job)) {
        // The same reason channel as initialize_owned's other "DLSS unavailable" cases.
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "%s", "the NGX thread could not be started");
        caps.dlss_available = 0;
    }
}

bool Dlss::optimal(unsigned out_w, unsigned out_h, int mode, unsigned& render_w, unsigned& render_h) noexcept {
    bool result = false;
    auto job = [&] { result = optimal_owned(out_w, out_h, mode, render_w, render_h); };
    run(job);
    return result;
}

bool Dlss::evaluate(ID3D11DeviceContext* context, const x64remote::DlssEvaluateRecord& record, ID3D11Resource* color,
                    ID3D11Resource* depth, ID3D11Resource* motion, ID3D11Resource* output, ID3D11Resource* exposure,
                    char* error, std::size_t error_bytes) noexcept {
    bool result = false;
    auto job = [&] { result = evaluate_owned(context, record, color, depth, motion, output, exposure, error, error_bytes); };
    if (!run(job)) std::snprintf(error, error_bytes, "DLSS unavailable: the NGX thread could not be started");
    return result;
}

void Dlss::initialize_d3d12(ID3D12Device* device, x64remote::Capabilities& caps, std::function<void()> wait_idle) noexcept {
    wait_idle_ = std::move(wait_idle);
    auto job = [&] { initialize_d3d12_owned(device, caps); };
    if (!run(job)) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "%s", "the NGX thread could not be started");
        caps.dlss_available = 0;
    }
}

#ifdef EID3D11_EMBEDDED
void Dlss::initialize_embedded_d3d12(ID3D12Device* device, const std::wstring& dll_folder,
    const std::wstring& data_folder, x64remote::Capabilities& caps, Log log,
    std::function<void()> wait_idle) noexcept {
    // Old features wait on the old executor before any callback/path changes.
    shutdown();
    caps.dlss_available = 0;
    if (!device || dll_folder.empty() || data_folder.empty() || !log || !wait_idle) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "Invalid embedded NGX device, paths, logger or GPU wait");
        return;
    }
    try {
        folder_ = dll_folder;
        data_folder_ = data_folder;
        wait_idle_ = std::move(wait_idle);
    } catch (...) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "Embedded NGX initialization allocation failed");
        return;
    }
    log_ = log;
    auto job = [&] { initialize_d3d12_owned(device, caps); };
    if (!run(job)) {
        std::snprintf(caps.dlss_reason, sizeof(caps.dlss_reason), "the NGX thread could not be started");
        caps.dlss_available = 0;
    }
}
#endif

bool Dlss::evaluate_d3d12(ID3D12GraphicsCommandList* list, const x64remote::DlssEvaluateRecord& record,
                          ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept {
    bool result = false;
    auto job = [&] { result = evaluate_d3d12_owned(list, record, resources, error, error_bytes); };
    if (!run(job)) std::snprintf(error, error_bytes, "DLSS unavailable: the NGX thread could not be started");
    return result;
}

void Dlss::release() noexcept {
    if (!worker_.joinable()) return; // no NGX thread: NGX was never initialised
    auto job = [&] { release_owned(); };
    run(job);
}

void Dlss::shutdown() noexcept {
    if (worker_.joinable()) {
        auto job = [&] { shutdown_owned(); };
        run(job);
    }
    stop_worker();
}

} // namespace remote
