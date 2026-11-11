// FSR 3.1 frame generation context; see d3d12_fsr_fg.h.
#include "d3d12_fsr_fg.h"

#include <ffx_api.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace d3d12 {

namespace {

// The FG swap chain asks for the interpolation through this callback,
// during Present on the game thread (frame-interpolation-swap-chain.md).
std::atomic<std::uint64_t> g_dispatches{0};

ffxReturnCode_t dispatch_generation(ffxDispatchDescFrameGeneration* params, void* context) {
    const ffxReturnCode_t result = ffxDispatch(static_cast<ffxContext*>(context), &params->header);
    if (result == FFX_API_RETURN_OK) g_dispatches.fetch_add(1, std::memory_order_relaxed);
    return result;
}

// The FSR 3.x frame generation provider (the loader may also list ML frame
// generation 4.x, which needs RDNA4 and the Agility SDK); 0: none.
std::uint64_t fsr3_version(ID3D12Device* device, char* name, std::size_t name_bytes) noexcept {
    std::uint64_t count = 8, ids[8]{};
    const char* names[8]{};
    ffxQueryDescGetVersions versions{};
    versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    versions.device = device;
    versions.outputCount = &count;
    versions.versionIds = ids;
    versions.versionNames = names;
    if (ffxQuery(nullptr, &versions.header) != FFX_API_RETURN_OK) return 0;
    for (std::uint64_t i = 0; i < count && i < 8; ++i)
        if (names[i] && names[i][0] == '3') {  // listed newest first
            std::snprintf(name, name_bytes, "%s", names[i]);
            return ids[i];
        }
    return 0;
}

} // namespace

bool FsrFrameGeneration::available(ID3D12Device* device, std::string& reason) noexcept {
    // Delay-loaded: the DLLs must exist before the first FidelityFX call.
    static int state = -1;  // -1 unknown, 0 no, 1 yes
    static char text[200] = {};
    if (state < 0) {
        state = 0;
        if (!LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll") || !LoadLibraryW(L"amd_fidelityfx_framegeneration_dx12.dll")) {
            std::snprintf(text, sizeof(text), "%s", "the FidelityFX DLLs are missing beside the render host");
        } else {
            char name[64] = {};
            if (!fsr3_version(device, name, sizeof(name))) std::snprintf(text, sizeof(text), "%s", "FidelityFX offers no FSR 3 frame generation on this device");
            else state = 1;
        }
    }
    if (!state) reason = text;
    return state == 1;
}

bool FsrFrameGeneration::create(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format, std::string& reason) noexcept {
    destroy(nullptr);
    char name[64] = {};
    const std::uint64_t version = fsr3_version(device, name, sizeof(name));
    if (!version) { reason = "FidelityFX offers no FSR 3 frame generation on this device"; return false; }
    ffxOverrideVersion override_version{};
    override_version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    override_version.versionId = version;
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.header.pNext = &override_version.header;
    backend.device = device;
    ffxCreateContextDescFrameGeneration desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    desc.header.pNext = &backend.header;
    desc.flags = 0;  // depth standard (not inverted, finite), motion at render size without jitter, SDR
    desc.displaySize = {width, height};
    desc.maxRenderSize = {width, height};
    desc.backBufferFormat = ffxApiGetSurfaceFormatDX12(format);
    ffxContext context = nullptr;
    const ffxReturnCode_t created = ffxCreateContext(&context, &desc.header, nullptr);
    if (created != FFX_API_RETURN_OK || !context) {
        char text[160];
        std::snprintf(text, sizeof(text), "FidelityFX frame generation %s context %ux%u could not be created (ffx %u)", name, width, height, unsigned(created));
        reason = text;
        return false;
    }
    context_ = context;
    width_ = width;
    height_ = height;
    prepared_ = false;
    char text[160];
    std::snprintf(text, sizeof(text), "%s", name);
    reason = text;  // the provider's name for the caller's log line
    return true;
}

void FsrFrameGeneration::destroy(IDXGISwapChain* swap) noexcept {
    if (!context_) return;
    ffxContext context = static_cast<ffxContext>(context_);
    if (swap) {
        ffxConfigureDescFrameGeneration off{};
        off.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        off.swapChain = swap;
        off.frameGenerationEnabled = false;
        off.frameID = ++frame_id_;
        ffxConfigure(&context, &off.header);  // waits for the swap chain's presents
    }
    ffxDestroyContext(&context, nullptr);
    context_ = nullptr;
    width_ = height_ = 0;
    prepared_ = enabled_ = false;
}

bool FsrFrameGeneration::prepare(ID3D12GraphicsCommandList* list, ID3D12Resource* depth, ID3D12Resource* motion, const Prepare& p,
                                 std::string& error) noexcept {
    if (!context_) { error = "no frame generation context"; return false; }
    ffxContext context = static_cast<ffxContext>(context_);
    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = frame_id_ + 1;
    prepare.commandList = list;
    prepare.renderSize = {p.render_w, p.render_h};
    prepare.jitterOffset = {p.jitter_x, p.jitter_y};
    prepare.motionVectorScale = {1.f, 1.f};  // render pixels (frame-interpolation-api.md)
    prepare.frameTimeDelta = p.frame_ms;
    prepare.reset = p.reset;
    prepare.cameraNear = p.near_plane;
    prepare.cameraFar = p.far_plane;
    prepare.cameraFovAngleVertical = p.fov_y;
    prepare.viewSpaceToMetersFactor = 1.f;
    prepare.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    prepare.motionVectors = ffxApiGetResourceDX12(motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    std::memcpy(prepare.cameraPosition, p.position, sizeof(p.position));
    std::memcpy(prepare.cameraUp, p.up, sizeof(p.up));
    std::memcpy(prepare.cameraRight, p.right, sizeof(p.right));
    std::memcpy(prepare.cameraForward, p.forward, sizeof(p.forward));
    const ffxReturnCode_t result = ffxDispatch(&context, &prepare.header);
    if (result != FFX_API_RETURN_OK) {
        char text[96];
        std::snprintf(text, sizeof(text), "FidelityFX frame generation prepare failed (ffx %u)", unsigned(result));
        error = text;
        return false;
    }
    prepared_ = true;
    return true;
}

bool FsrFrameGeneration::configure(IDXGISwapChain* swap, ID3D12Resource* hudless, std::string& error) noexcept {
    if (!context_) return true;
    const bool enabled = prepared_ && hudless;
    prepared_ = false;
    // Consecutive frames without generation need no configure (and the
    // disabling one waits for the presents): frame ids then skip, which
    // resets generation when it turns on again (wanted after such frames).
    if (!enabled && !enabled_) { ++frame_id_; return true; }
    ffxContext context = static_cast<ffxContext>(context_);
    ffxConfigureDescFrameGeneration configure{};
    configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configure.swapChain = swap;
    configure.frameGenerationCallback = dispatch_generation;
    configure.frameGenerationCallbackUserContext = context_ ? &context_ : nullptr;
    configure.frameGenerationEnabled = enabled;
    configure.allowAsyncWorkloads = false;
    if (enabled) configure.HUDLessColor = ffxApiGetResourceDX12(hudless, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    configure.generationRect = {0, 0, int(width_), int(height_)};
    configure.frameID = ++frame_id_;
    const ffxReturnCode_t result = ffxConfigure(&context, &configure.header);
    if (result != FFX_API_RETURN_OK) {
        char text[96];
        std::snprintf(text, sizeof(text), "FidelityFX frame generation configure failed (ffx %u)", unsigned(result));
        error = text;
        return false;
    }
    enabled_ = enabled;
    return true;
}

std::uint64_t FsrFrameGeneration::dispatches() noexcept { return g_dispatches.load(std::memory_order_relaxed); }

std::uint64_t FsrFrameGeneration::vram() const noexcept {
    if (!context_) return 0;
    ffxContext context = static_cast<ffxContext>(context_);
    FfxApiEffectMemoryUsage usage{};
    ffxQueryDescFrameGenerationGetGPUMemoryUsage query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE;
    query.gpuMemoryUsageFrameGeneration = &usage;
    return ffxQuery(&context, &query.header) == FFX_API_RETURN_OK ? usage.totalUsageInBytes : 0;
}

} // namespace d3d12
