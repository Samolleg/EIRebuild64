#pragma once
// DLSS Super Resolution in the x64 host (NGX is 64-bit only). The renderer
// reaches it through x64remote::dlss_* (Source/x64_remote_link.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "../Source/x64_remote_protocol.h"
#include "../Source/x64_remote_link.h"

#include <nvsdk_ngx_defs.h>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace remote {

class Dlss {
public:
    using Log = void (*)(const char* text);
    // After device creation: NGX init and the DLSS capability, written into
    // caps.dlss_available / dlss_reason (the client's snapshot).
    void initialize(ID3D11Device* device, const std::wstring& folder, x64remote::Capabilities& caps, Log log) noexcept;
    // Optimal render size for an output size and API mode (0 performance,
    // 1 balanced, 2 quality, 3 ultra performance, 4 DLAA).
    bool optimal(unsigned out_w, unsigned out_h, int mode, unsigned& render_w, unsigned& render_h) noexcept;
    // Evaluates on the immediate context (NGX preserves its state),
    // recreating the feature when its shape changes. False with `error`.
    bool evaluate(ID3D11DeviceContext* context, const x64remote::DlssEvaluateRecord& record, ID3D11Resource* color,
                  ID3D11Resource* depth, ID3D11Resource* motion, ID3D11Resource* output, ID3D11Resource* exposure,
                  char* error, std::size_t error_bytes) noexcept;
    // renderer_backend=d3d12: NGX moves to the executor's device (the D3D11
    // instance initialize made is shut down first, the same folder), caps
    // rewritten as initialize writes them. wait_idle waits for that device's
    // submitted work: run before a feature the GPU may still use is released.
    void initialize_d3d12(ID3D12Device* device, x64remote::Capabilities& caps, std::function<void()> wait_idle) noexcept;
    // One evaluation recorded into list (the D3D12 backend). resources:
    // colour, depth, motion, output, already in their states (shader read,
    // depth read, shader read, unordered access). The feature is recreated
    // when its shape changes, as evaluate does.
    bool evaluate_d3d12(ID3D12GraphicsCommandList* list, const x64remote::DlssEvaluateRecord& record,
                        ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept;
    // DLSS Frame Generation on the D3D12 device (nvngx_dlssg.dll beside the
    // host exe): availability from initialize_d3d12's capability query.
    bool frame_generation_caps(x64remote::DlssgCaps& out) noexcept;
    // One generated frame recorded into list (resources as
    // x64remote::dlssg_evaluate_d3d12); the feature is created on first use
    // and recreated when its sizes change. False with `error`.
    bool evaluate_frame_generation_d3d12(ID3D12GraphicsCommandList* list, const x64remote::DlssgEvaluate& record,
                                         ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept;
    void release_frame_generation() noexcept;
    std::uint64_t frame_generation_vram(unsigned width, unsigned height, unsigned render_w, unsigned render_h) noexcept;
    // Releases the feature (DLSS turned off); NGX stays initialised and the
    // next evaluation creates a new one.
    void release() noexcept;
    // Releases the feature, the parameter maps and NGX (device teardown).
    void shutdown() noexcept;
    Dlss() = default;
    Dlss(const Dlss&) = delete;
    Dlss& operator=(const Dlss&) = delete;
    ~Dlss() { shutdown(); }

private:
    struct Shape {
        unsigned render_w = 0, render_h = 0, out_w = 0, out_h = 0;
        int mode = -1;
        int flags = 0;
        bool operator==(const Shape& other) const noexcept {
            return render_w == other.render_w && render_h == other.render_h && out_w == other.out_w &&
                   out_h == other.out_h && mode == other.mode && flags == other.flags;
        }
    };
    ID3D11Device* device_ = nullptr;
    ID3D12Device* device12_ = nullptr;  // set: NGX runs on the D3D12 device (every call takes its D3D12 form)
    std::function<void()> wait_idle_;
    Log log_ = nullptr;
    bool initialized_ = false, available_ = false;
    NVSDK_NGX_Parameter* capabilities_ = nullptr; // GetCapabilityParameters: optimal settings
    NVSDK_NGX_Parameter* parameters_ = nullptr;   // AllocateParameters: create and evaluate
    NVSDK_NGX_Handle* feature_ = nullptr;
    // Frame generation: availability (initialize_d3d12) and its feature.
    bool fg_available_ = false;
    int fg_multi_frame_max_ = 0;
    char fg_reason_[160] = "NGX was not initialised on Direct3D 12";
    NVSDK_NGX_Handle* fg_feature_ = nullptr;
    unsigned fg_width_ = 0, fg_height_ = 0, fg_render_w_ = 0, fg_render_h_ = 0;
    void query_frame_generation() noexcept;
    bool evaluate_frame_generation_owned(ID3D12GraphicsCommandList* list, const x64remote::DlssgEvaluate& record,
                                         ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept;
    void release_frame_generation_owned() noexcept;
    Shape shape_;
    // NGX bodies, run only on the NGX thread (dlss.cpp).
    void initialize_owned(ID3D11Device* device, const std::wstring& folder, x64remote::Capabilities& caps, Log log) noexcept;
    bool optimal_owned(unsigned out_w, unsigned out_h, int mode, unsigned& render_w, unsigned& render_h) noexcept;
    bool evaluate_owned(ID3D11DeviceContext* context, const x64remote::DlssEvaluateRecord& record, ID3D11Resource* color,
                        ID3D11Resource* depth, ID3D11Resource* motion, ID3D11Resource* output, ID3D11Resource* exposure,
                        char* error, std::size_t error_bytes) noexcept;
    void initialize_d3d12_owned(ID3D12Device* device, x64remote::Capabilities& caps) noexcept;
    bool evaluate_d3d12_owned(ID3D12GraphicsCommandList* list, const x64remote::DlssEvaluateRecord& record,
                              ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept;
    // The capability and parameter maps after an API's init (both forms).
    void query_capabilities(x64remote::Capabilities& caps) noexcept;
    // Releases the feature in the API NGX runs on (D3D12: after the GPU is idle).
    void release_feature() noexcept;
    void release_owned() noexcept;
    void shutdown_owned() noexcept;
    // Runs job(context) on the NGX thread (started on first use) and waits.
    // False (job not run) when the thread could not be started: DLSS is then
    // unavailable; NGX never runs on another thread.
    bool run_raw(void (*job)(void*), void* context) noexcept;
    template <class F> bool run(F& job) noexcept { return run_raw([](void* f) { (*static_cast<F*>(f))(); }, &job); }
    void stop_worker() noexcept;
    std::thread worker_;
    std::atomic<DWORD> worker_id_{0};
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    void (*job_)(void*) = nullptr;
    void* job_context_ = nullptr;
    bool stop_ = false, worker_failed_ = false;
    // The feature info NGX was initialised with, its path list and the folder
    // string it points at live as long as NGX: the SDK reserves InternalData
    // in it for NGX, and feature creation (first evaluate) searches the path
    // list for nvngx_dlss.dll. They were stack locals and a temporary.
    std::wstring folder_;
    const wchar_t* paths_[1]{};
    NVSDK_NGX_FeatureCommonInfo info_{};
};

} // namespace remote
