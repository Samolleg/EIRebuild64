// FSR 3.1 frame generation context; see d3d12_fsr_fg.h.
#include "d3d12_fsr_fg.h"

#include "d3d12_fidelityfx_runtime.h"
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>

#include <atomic>
#include <mutex>
#include "frame_waits.h"
#include <cstdio>
#include <cstring>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12::fidelityfx {
Api api;

bool load(std::string& reason) noexcept {
    // Keep successful loads for the process lifetime: proxy worker threads
    // and callbacks may outlive an individual frame-generation context.
    static const std::string failure = [] {
        Api resolved;
        HMODULE loader = nullptr, provider = nullptr;
        DWORD error = ERROR_SUCCESS;
        const wchar_t* failed = L"amd_fidelityfx_loader_dx12.dll";
        constexpr DWORD flags = LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
        loader = LoadLibraryExW(failed, nullptr, flags);
        if (!loader) error = GetLastError();
        else {
            failed = L"amd_fidelityfx_framegeneration_dx12.dll";
            provider = LoadLibraryExW(failed, nullptr, flags);
            if (!provider) error = GetLastError();
        }
        const char* symbol = nullptr;
        if (loader && provider) {
#define FFX_RESOLVE(member, type, name) \
            if (!symbol) { \
                resolved.member = reinterpret_cast<type>(GetProcAddress(loader, name)); \
                if (!resolved.member) { error = GetLastError(); symbol = name; } \
            }
            FFX_RESOLVE(create, PfnFfxCreateContext, "ffxCreateContext");
            FFX_RESOLVE(destroy, PfnFfxDestroyContext, "ffxDestroyContext");
            FFX_RESOLVE(configure, PfnFfxConfigure, "ffxConfigure");
            FFX_RESOLVE(query, PfnFfxQuery, "ffxQuery");
            FFX_RESOLVE(dispatch, PfnFfxDispatch, "ffxDispatch");
#undef FFX_RESOLVE
            if (!symbol) { api = resolved; return std::string{}; }
            failed = L"amd_fidelityfx_loader_dx12.dll";
        }
        char text[512];
        std::snprintf(text, sizeof(text), "FSR FG unavailable: %ls%s%s (Windows error %lu). Install VC++ Redistributable 2015+ x64 and check FidelityFX DLLs beside the EXE",
                      failed, symbol ? ": missing export " : " failed to load", symbol ? symbol : "", static_cast<unsigned long>(error));
        if (provider) FreeLibrary(provider);
        if (loader) FreeLibrary(loader);
        remote::host_log("d3d12: %s", text);
        return std::string(text);
    }();
    if (!failure.empty()) { reason = failure; return false; }
    return true;
}
} // namespace d3d12::fidelityfx

namespace d3d12 {

namespace {

// The FG swap chain asks for the interpolation through this callback,
// during Present on the game thread (frame-interpolation-swap-chain.md).
std::atomic<std::uint64_t> g_dispatches{0};
std::mutex g_gpu_mutex;
owned::frame_waits::Distribution g_gpu_ms;
std::uint64_t g_gpu_invalid = 0;

ffxReturnCode_t dispatch_generation_callback(ffxDispatchDescFrameGeneration* params, void* context) {
    return static_cast<ffxReturnCode_t>(static_cast<FsrFrameGeneration*>(context)->dispatch_generation(params));
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
    if (fidelityfx::api.query(nullptr, &versions.header) != FFX_API_RETURN_OK) return 0;
    for (std::uint64_t i = 0; i < count && i < 8; ++i)
        if (names[i] && names[i][0] == '3') {  // listed newest first
            std::snprintf(name, name_bytes, "%s", names[i]);
            return ids[i];
        }
    return 0;
}

} // namespace

bool FsrFrameGeneration::available(ID3D12Device* device, std::string& reason) noexcept {
    if (!fidelityfx::load(reason)) return false;
    static int state = -1;  // -1 unknown, 0 no, 1 yes
    static char text[200] = {};
    if (state < 0) {
        state = 0;
        char name[64] = {};
        if (!fsr3_version(device, name, sizeof(name))) std::snprintf(text, sizeof(text), "%s", "FidelityFX offers no FSR 3 frame generation on this device");
        else state = 1;
    }
    if (!state) reason = text;
    return state == 1;
}

bool FsrFrameGeneration::create(ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height, DXGI_FORMAT format, std::string& reason) noexcept {
    destroy(nullptr);
    if (!available(device, reason)) return false;
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
    const ffxReturnCode_t created = fidelityfx::api.create(&context, &desc.header, nullptr);
    if (created != FFX_API_RETURN_OK || !context) {
        char text[160];
        std::snprintf(text, sizeof(text), "FidelityFX frame generation %s context %ux%u could not be created (ffx %u)", name, width, height, unsigned(created));
        reason = text;
        return false;
    }
    // Diagnostics may be unavailable without disabling generation. Only the FG
    // pass allocates new queries. Never wait for their results on the CPU.
    std::string timing_reason;
    try {
        if (RuntimeDiagnostics::Enabled() && queue && gpu_times_.create(device, queue, timing_reason, 1) &&
            SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu_fence_)))) {
            gpu_queue_ = queue;
        }
    } catch (...) {}
    if (RuntimeDiagnostics::Enabled() && !gpu_queue_) {
        std::lock_guard<std::mutex> lock(g_gpu_mutex);
        ++g_gpu_invalid;
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
        fidelityfx::api.configure(&context, &off.header);  // waits for the swap chain's presents
    }
    fidelityfx::api.destroy(&context, nullptr);
    // SDK destruction drains its lists before profiler query resources go away.
    collect_gpu_times();
    gpu_queue_.Reset(); gpu_fence_.Reset();
    for (auto& value : gpu_values_) value = 0;
    gpu_serial_ = 0; gpu_slot_ = 0; gpu_recorded_ = false;
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
    const ffxReturnCode_t result = fidelityfx::api.dispatch(&context, &prepare.header);
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
    configure.frameGenerationCallback = dispatch_generation_callback;
    configure.frameGenerationCallbackUserContext = this;
    configure.frameGenerationEnabled = enabled;
    configure.allowAsyncWorkloads = false;
    if (enabled) configure.HUDLessColor = ffxApiGetResourceDX12(hudless, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    configure.generationRect = {0, 0, int(width_), int(height_)};
    configure.frameID = ++frame_id_;
    const ffxReturnCode_t result = fidelityfx::api.configure(&context, &configure.header);
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
    return fidelityfx::api.query(&context, &query.header) == FFX_API_RETURN_OK ? usage.totalUsageInBytes : 0;
}

void FsrFrameGeneration::collect_gpu_times() noexcept {
    if (!gpu_fence_) return;
    const auto completed = gpu_fence_->GetCompletedValue();
    if (completed == UINT64_MAX) return;
    for (unsigned slot = 0; slot < Core::kFramesInFlight; ++slot) {
        if (!gpu_values_[slot] || gpu_values_[slot] > completed) continue;
        try {
            std::string reason;
            const bool ready = gpu_times_.begin_frame(slot, reason);
            std::lock_guard<std::mutex> lock(g_gpu_mutex);
            if (ready && gpu_times_.last().size() == 1) g_gpu_ms.add(gpu_times_.last()[0].ms);
            else ++g_gpu_invalid;
        } catch (...) {
            std::lock_guard<std::mutex> lock(g_gpu_mutex);
            ++g_gpu_invalid;
        }
        gpu_values_[slot] = 0;
    }
}

unsigned FsrFrameGeneration::dispatch_generation(void* parameters) noexcept {
    auto* params = static_cast<ffxDispatchDescFrameGeneration*>(parameters);
    auto* list = static_cast<ID3D12GraphicsCommandList*>(params->commandList);
    collect_gpu_times();
    bool timed = false;
    std::uint32_t pass = 0;
    // A busy query slot drops a diagnostic sample without delaying the frame.
    if (gpu_queue_ && !gpu_recorded_ && !gpu_values_[gpu_slot_]) {
        try {
            std::string reason;
            if (gpu_times_.begin_frame(gpu_slot_, reason)) {
                pass = gpu_times_.begin(list, "fg_fsr_interpolate");
                timed = true;
            }
        } catch (...) {}
    }
    ffxContext context = static_cast<ffxContext>(context_);
    const auto result = fidelityfx::api.dispatch(&context, &params->header);
    if (timed) {
        gpu_times_.end(list, pass);
        gpu_times_.resolve(list);
        gpu_recorded_ = true;
    } else if (RuntimeDiagnostics::Enabled()) {
        std::lock_guard<std::mutex> lock(g_gpu_mutex);
        ++g_gpu_invalid;
    }
    if (result == FFX_API_RETURN_OK) g_dispatches.fetch_add(1, std::memory_order_relaxed);
    return static_cast<unsigned>(result);
}

void FsrFrameGeneration::submitted() noexcept {
    if (gpu_recorded_) {
        // allowAsyncWorkloads=false records interpolation on the game queue.
        // Present has submitted the callback list before this queue signal.
        const auto value = ++gpu_serial_;
        if (SUCCEEDED(gpu_queue_->Signal(gpu_fence_.Get(), value))) gpu_values_[gpu_slot_] = value;
        else {
            std::lock_guard<std::mutex> lock(g_gpu_mutex);
            ++g_gpu_invalid;
            gpu_queue_.Reset(); // no reuse after losing the completion signal
        }
        gpu_slot_ = (gpu_slot_ + 1) % Core::kFramesInFlight;
        gpu_recorded_ = false;
    }
    collect_gpu_times();
}

std::string FsrFrameGeneration::take_gpu_report() {
    std::lock_guard<std::mutex> lock(g_gpu_mutex);
    std::string result = "gpu_fsr_interpolate_scope=callback_dispatch gpu_fsr_interpolate_in_sum=0";
    g_gpu_ms.append(result, "gpu_fsr_interpolate");
    result += " gpu_fsr_interpolate_invalid_n=" + std::to_string(g_gpu_invalid);
    g_gpu_invalid = 0;
    return result;
}

} // namespace d3d12
