#include "embedded_d3d12.h"
#include "frame_waits.h"
#include "dlss_frame.h"
#include "../Host/dlss.h"
#include "../Host/reflex.h"
#include "engine_bridge.h"
#include <shlobj.h>
#include <d3d12.h>
#include <cstdarg>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <filesystem>

extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 620;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace d3d12 {
// Factory implemented by the author's Host/d3d12/d3d12_frame.cpp.
std::unique_ptr<owned::D3D12Frame> create_frame_executor(HWND, const std::wstring&, std::string&);
const std::string& frame_executor_failure(owned::D3D12Frame&) noexcept;
}

namespace remote {
std::atomic<std::uint64_t> g_startup_pipeline_progress{0};
// Preserve the author's diagnostic call sites; route them to the engine's
// journal instead of creating host.log or a second process.
void host_log(const char* format, ...) {
    char text[4096]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    owned::bridge_report_event("d3d12", text);
}
}

namespace owned {
namespace {
void ngx_log(const char* text) { bridge_report_event("d3d12_ngx", text); }
bool directory(const std::wstring& path, std::string& reason) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    const DWORD error = GetLastError();
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (error == ERROR_ALREADY_EXISTS && attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
    char text[128];
    std::snprintf(text, sizeof(text), "Cannot create EIRebuild64 D3D12 data directory (Win32 %lu)", error);
    reason = text;
    return false;
}
}

struct EmbeddedD3D12::State {
    // Reverse destruction releases NGX before the executor and its device.
    std::unique_ptr<D3D12Frame> executor;
    remote::Dlss ngx;
    x64remote::Capabilities caps{};
    enum class Phase { idle, simulation, rendering } phase = Phase::idle;
    std::uint64_t serial = 0;
    std::int64_t simulation_start = 0;
};

EmbeddedD3D12::EmbeddedD3D12() = default;
EmbeddedD3D12::~EmbeddedD3D12() { shutdown(); }

bool EmbeddedD3D12::initialize(HWND window, std::string& reason) noexcept {
    shutdown();
    try {
        reason.clear();
        if (!window || !IsWindow(window)) { reason = "D3D12 requires the native game window"; return false; }
        wchar_t exe[32768]{};
        const DWORD count = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
        if (!count || count >= ARRAYSIZE(exe)) { reason = "D3D12 executable path unavailable"; return false; }
        wchar_t* slash = std::wcsrchr(exe, L'\\');
        if (!slash) { reason = "D3D12 executable directory unavailable"; return false; }
        *slash = 0;
        wchar_t local[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) {
            reason = "D3D12 writable data directory unavailable";
            return false;
        }
        const std::wstring base = std::wstring(local) + L"\\EIRebuild64";
        const std::wstring cache = base + L"\\D3D12";
        const std::wstring ngx_data = (std::filesystem::path(exe).parent_path() / L"Logs64").wstring();
        if (!directory(base, reason) || !directory(cache, reason) || !directory(ngx_data, reason)) return false;
        auto state = std::make_unique<State>();
        state->executor = d3d12::create_frame_executor(window, cache + L"\\", reason);
        if (!state->executor) return false;
        D3D12Frame* executor = state->executor.get();
        state->ngx.initialize_embedded_d3d12(executor->device(), exe, ngx_data, state->caps, &ngx_log,
                                           [executor] { return executor->wait_idle(); },
                                           [executor](std::uint64_t& recording, std::uint64_t& completed) {
                                               executor->fence_state(recording, completed);
                                           });
        // NGX absence disables only the NVIDIA features. The selected D3D12
        // executor remains the owner, including on AMD/Intel GPUs.
        state_ = std::move(state);
        return true;
    } catch (...) {
        reason = "D3D12 initialization allocation failed";
        return false;
    }
}

D3D12Frame* EmbeddedD3D12::frame() const noexcept { return state_ ? state_->executor.get() : nullptr; }
HRESULT EmbeddedD3D12::begin_simulation(int reflex_mode) noexcept {
    if (!state_) return E_UNEXPECTED;
    if (state_->phase == State::Phase::rendering) return S_FALSE;
    if (state_->phase == State::Phase::simulation) return S_OK;
    // Author's Sleep-before-game-release boundary, now before local input.
    reflex::configure(state_->executor->device(), reflex_mode);
    { const frame_waits::Scope wait(frame_waits::reflex); reflex::sleep(); }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    state_->simulation_start = now.QuadPart;
    ++state_->serial;
    reflex::marker(reflex::Marker::simulation_start, state_->serial);
    state_->phase = State::Phase::simulation;
    return S_OK;
}
HRESULT EmbeddedD3D12::begin_render(int reflex_mode) noexcept {
    if (!state_) return E_UNEXPECTED;
    if (state_->ngx.poll_initialize(state_->caps))
        set_dlss_unavailable_reason(state_->caps.dlss_available ? nullptr : state_->caps.dlss_reason);
    // A nested loading draw seals the prior engine invocation at this point.
    if (state_->phase == State::Phase::rendering) finish_render();
    // Loading movies and repeated capture/present calls can draw without an
    // outer game simulation. Give each such render its own local frame ID.
    if (state_->phase == State::Phase::idle) {
        const HRESULT hr = begin_simulation(reflex_mode);
        if (FAILED(hr)) return hr;
    }
    if (state_->phase != State::Phase::simulation) return E_UNEXPECTED;
    // The executor and asynchronous pacer receive the real simulation QPC.
    reflex::set_frame(state_->serial, state_->simulation_start);
    reflex::marker(reflex::Marker::simulation_end, state_->serial);
    reflex::marker(reflex::Marker::render_start, state_->serial);
    state_->phase = State::Phase::rendering;
    return S_OK;
}
void EmbeddedD3D12::finish_render() noexcept {
    if (!state_ || state_->phase == State::Phase::idle) return;
    // A skipped draw still closes its simulation, without a present sample.
    if (state_->phase == State::Phase::simulation) {
        reflex::marker(reflex::Marker::simulation_end, state_->serial);
        state_->phase = State::Phase::idle;
        return;
    }
    // Deduplicated by Reflex when executor::present already sent render_end.
    // Also close frames that never reached Present, as core_host does.
    reflex::marker(reflex::Marker::render_end, state_->serial);
    state_->phase = State::Phase::idle;
}
bool EmbeddedD3D12::dlss_caps(x64remote::DlssCaps& out) const noexcept {
    out = {};
    if (!state_) { std::snprintf(out.reason, sizeof(out.reason), "Embedded D3D12 is not initialized"); return false; }
    out.initializing = !state_->ngx.poll_initialize(state_->caps);
    out.available = state_->caps.dlss_available != 0;
    std::snprintf(out.reason, sizeof(out.reason), "%s", state_->caps.dlss_reason);
    return out.available;
}
bool EmbeddedD3D12::dlss_rr_caps(char* reason, std::size_t reason_bytes) noexcept {
    if (state_) return state_->ngx.ray_reconstruction_caps(reason, reason_bytes);
    std::snprintf(reason, reason_bytes, "%s", "Embedded D3D12 is not initialized");
    return false;
}
bool EmbeddedD3D12::dlss_size(UINT width, UINT height, int mode, UINT& w, UINT& h, bool ray_reconstruction) noexcept {
    w = h = 0;
    unsigned rw = 0, rh = 0;
    if (!state_ || !state_->ngx.optimal(width, height, mode, rw, rh, ray_reconstruction)) return false;
    w = rw; h = rh;
    return true;
}
HRESULT EmbeddedD3D12::dlss_evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources,
                                    const x64remote::DlssEvaluate& e) noexcept {
    if (!state_ || !list || !resources) return E_INVALIDARG;
    x64remote::DlssEvaluateRecord record{};
    record.jitter_x = e.jitter_x; record.jitter_y = e.jitter_y;
    record.mv_scale_x = e.mv_scale_x; record.mv_scale_y = e.mv_scale_y;
    record.render_w = e.render_w; record.render_h = e.render_h; record.out_w = e.out_w; record.out_h = e.out_h;
    record.mode = e.mode; record.sharpness = e.sharpness;
    record.flags = (e.reset ? x64remote::dlss_reset : 0u) | (e.hdr ? x64remote::dlss_hdr : 0u) |
                   (e.depth_inverted ? x64remote::dlss_depth_inverted : 0u) |
                   (e.bias_mask ? x64remote::dlss_bias_mask : 0u) |
                   (e.ray_reconstruction ? x64remote::dlss_ray_reconstruction : 0u) |
                   (e.specular_hit_distance ? x64remote::dlss_specular_hit_distance : 0u);
    std::copy(std::begin(e.world_to_view),std::end(e.world_to_view),std::begin(record.world_to_view));
    std::copy(std::begin(e.view_to_clip),std::end(e.view_to_clip),std::begin(record.view_to_clip));
    char error[200]{};
    if (state_->ngx.evaluate_d3d12(list, record, resources, error, sizeof(error))) return S_OK;
    ngx_log(error);
    return E_FAIL;
}
void EmbeddedD3D12::dlss_release() noexcept { if (state_) state_->ngx.release(); }
bool EmbeddedD3D12::fg_caps(x64remote::DlssgCaps& out) noexcept {
    out = {};
    if (state_) return state_->ngx.frame_generation_caps(out);
    std::snprintf(out.reason, sizeof(out.reason), "Embedded D3D12 is not initialized");
    return false;
}
HRESULT EmbeddedD3D12::fg_evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources,
                                  const x64remote::DlssgEvaluate& e) noexcept {
    if (!state_ || !list || !resources) return E_INVALIDARG;
    char error[200]{};
    if (state_->ngx.evaluate_frame_generation_d3d12(list, e, resources, error, sizeof(error))) return S_OK;
    ngx_log(error);
    return E_FAIL;
}
void EmbeddedD3D12::fg_release() noexcept { if (state_) state_->ngx.release_frame_generation(); }
std::uint64_t EmbeddedD3D12::fg_vram(UINT width, UINT height, UINT rw, UINT rh) noexcept {
    return state_ ? state_->ngx.frame_generation_vram(width, height, rw, rh) : 0;
}
const char* EmbeddedD3D12::failure() const noexcept {
    return state_ ? d3d12::frame_executor_failure(*state_->executor).c_str() : "Embedded D3D12 is not initialized";
}
HRESULT EmbeddedD3D12::device_removed_reason() const noexcept {
    ID3D12Device* device = state_ ? state_->executor->device() : nullptr;
    return device ? device->GetDeviceRemovedReason() : S_OK;
}
void EmbeddedD3D12::stop_ngx() noexcept {
    if (!state_) return;
    state_->ngx.shutdown();
    state_->caps.dlss_available = 0;
    std::snprintf(state_->caps.dlss_reason, sizeof(state_->caps.dlss_reason), "Embedded NGX was shut down");
}
void EmbeddedD3D12::shutdown() noexcept { state_.reset(); }
} // namespace owned
