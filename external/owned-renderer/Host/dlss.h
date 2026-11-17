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
#include <vector>

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
    // ray_reconstruction: Ray Reconstruction's own optimal settings (DLSS-RR
    // Integration Guide 5.5).
    bool optimal(unsigned out_w, unsigned out_h, int mode, unsigned& render_w, unsigned& render_h,
                 bool ray_reconstruction = false) noexcept;
    // Evaluates on the immediate context (NGX preserves its state),
    // recreating the feature when its shape changes. False with `error`.
    bool evaluate(ID3D11DeviceContext* context, const x64remote::DlssEvaluateRecord& record, ID3D11Resource* color,
                  ID3D11Resource* depth, ID3D11Resource* motion, ID3D11Resource* output, ID3D11Resource* exposure,
                  char* error, std::size_t error_bytes) noexcept;
    // renderer_backend=d3d12: starts asynchronously on the NGX thread, using
    // the host folder directly (no prior D3D11 init). The caller keeps the
    // device alive through shutdown, which joins initialization. Queries and
    // evaluations report pending without waiting; release is a no-op then
    // (no feature exists). wait_idle waits for that device's
    // submitted work: run before a feature the GPU may still use is released.
    // False from it: not finished (the release is then deferred, never early).
    // fence_state: the fence value the list being recorded will signal and
    // the value the GPU has completed (a feature recorded into the list that
    // is still open cannot be waited for: its release is deferred).
    using FenceState = std::function<void(std::uint64_t& recording, std::uint64_t& completed)>;
    void initialize_d3d12(ID3D12Device* device, const std::wstring& folder, x64remote::Capabilities& caps,
                          std::function<bool()> wait_idle, FenceState fence_state, Log log) noexcept;
    // Replay thread only: publish completed init's DLSS fields to caps once.
    // False while pending; never waits or overwrites adapter/device fields.
    bool poll_initialize(x64remote::Capabilities& caps) noexcept;
#ifdef EID3D11_EMBEDDED
    void initialize_embedded_d3d12(ID3D12Device* device, const std::wstring& dll_folder,
        const std::wstring& data_folder, x64remote::Capabilities& caps, Log log,
        std::function<bool()> wait_idle, FenceState fence_state) noexcept;
#endif
    // One evaluation recorded into list (the D3D12 backend). resources:
    // colour, depth, motion, output, already in their states (shader read,
    // depth read, shader read, unordered access). The feature is recreated
    // when its shape changes, as evaluate does. With dlss_ray_reconstruction
    // in record.flags the feature is DLSS Ray Reconstruction instead
    // (nvngx_dlssd.dll; DLSS-RR Integration Guide): resources[5] the world
    // normals with the linear roughness in alpha, [6] the diffuse albedo,
    // [7] the specular albedo, all render size and shader read.
    bool evaluate_d3d12(ID3D12GraphicsCommandList* list, const x64remote::DlssEvaluateRecord& record,
                        ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept;
    // DLSS Frame Generation on the D3D12 device (nvngx_dlssg.dll beside the
    // host exe): availability from initialize_d3d12's capability query.
    bool frame_generation_caps(x64remote::DlssgCaps& out) noexcept;
    // DLSS Ray Reconstruction on the D3D12 device (nvngx_dlssd.dll beside the
    // host exe): availability from initialize_d3d12's capability query; false
    // with the reason.
    bool ray_reconstruction_caps(char* reason, std::size_t reason_bytes) noexcept;
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
        bool ray_reconstruction = false;  // the feature is DLSS Ray Reconstruction, not Super Resolution
        bool operator==(const Shape& other) const noexcept {
            return render_w == other.render_w && render_h == other.render_h && out_w == other.out_w &&
                   out_h == other.out_h && mode == other.mode && flags == other.flags &&
                   ray_reconstruction == other.ray_reconstruction;
        }
    };
    ID3D11Device* device_ = nullptr;
    ID3D12Device* device12_ = nullptr;  // set: NGX runs on the D3D12 device (every call takes its D3D12 form)
    std::function<bool()> wait_idle_;
    FenceState fence_state_;
    Log log_ = nullptr;
    bool initialized_ = false, available_ = false;
    NVSDK_NGX_Parameter* capabilities_ = nullptr; // GetCapabilityParameters: optimal settings
    NVSDK_NGX_Parameter* parameters_ = nullptr;   // AllocateParameters: create and evaluate
    NVSDK_NGX_Handle* feature_ = nullptr;
    // DLSS Programming Guide 5.5: a feature handle is released only once the
    // work that evaluated it is no longer in flight. Direct3D 11:
    // release_handle first waits for the immediate context's submitted work.
    // Direct3D 12: it waits for the submitted frames; a feature recorded
    // (create or evaluate) into the list that is still open cannot be waited
    // for and stays in retired_ until the fence passes that list.
    struct Retired {
        NVSDK_NGX_Handle* feature;
        std::uint64_t recorded;  // the fence value of the list that last recorded it
        const char* what;
        ID3D11Query* done;  // Direct3D 11: the event query ended after its last evaluate (null: not made yet)
    };
    // A release happens only after a confirmed completion or a confirmed
    // device removal; until then the feature waits in retired_ (collected at
    // later NGX calls). One that cannot be confirmed even at teardown stays
    // unreleased, and NGX's shutdown is skipped (unconfirmed_).
    bool unconfirmed_ = false;
    ULONGLONG teardown_deadline_ = 0;  // set during shutdown_owned: its waits end here (5 s in total)
    bool d3d11_finished(ID3D11Query*& done, DWORD wait_ms) noexcept;
    void defer_release(const Retired& retired, const char* why) noexcept;
    std::vector<Retired> retired_;
    std::uint64_t feature_recorded_ = 0, fg_recorded_ = 0;
    void release_handle(NVSDK_NGX_Handle* feature, std::uint64_t recorded, unsigned evaluations, const char* what) noexcept;
    void collect_retired(bool all) noexcept;
    // Fault report (issue 193): one host.log line for each create, first
    // evaluate after a create and release, with the NGX result and the fence
    // state, so a crash near one of them names its state.
    unsigned evaluations_ = 0, fg_evaluations_ = 0;  // of the current feature
    bool hooks_logged_ = false;
    void log_first_evaluate(const char* what, unsigned result) noexcept;
    void log_overlay_hooks() noexcept;
    void fence_text(char* text, std::size_t bytes) noexcept;
    // Frame generation: availability (initialize_d3d12) and its feature.
    bool fg_available_ = false;
    int fg_multi_frame_max_ = 0;
    char fg_reason_[160] = "NGX was not initialised on Direct3D 12";
    NVSDK_NGX_Handle* fg_feature_ = nullptr;
    unsigned fg_width_ = 0, fg_height_ = 0, fg_render_w_ = 0, fg_render_h_ = 0;
    void query_frame_generation() noexcept;
    // Ray Reconstruction: availability (initialize_d3d12). Its feature is
    // feature_ (it replaces Super Resolution: shape_.ray_reconstruction).
    bool rr_available_ = false;
    char rr_reason_[160] = "NGX was not initialised on Direct3D 12";
    void query_ray_reconstruction() noexcept;
    const char* feature_name() const noexcept { return shape_.ray_reconstruction ? "DLSS Ray Reconstruction" : "DLSS"; }
    bool evaluate_frame_generation_owned(ID3D12GraphicsCommandList* list, const x64remote::DlssgEvaluate& record,
                                         ID3D12Resource* const* resources, char* error, std::size_t error_bytes) noexcept;
    void release_frame_generation_owned() noexcept;
    Shape shape_;
    // NGX bodies, run only on the NGX thread (dlss.cpp).
    void initialize_owned(ID3D11Device* device, const std::wstring& folder, x64remote::Capabilities& caps, Log log) noexcept;
    bool optimal_owned(unsigned out_w, unsigned out_h, int mode, unsigned& render_w, unsigned& render_h, bool ray_reconstruction) noexcept;
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
    // Only startup uses wait=false, with this object's lifetime-owned context.
    // False (job not run) when the thread could not be started: DLSS is then
    // unavailable; NGX never runs on another thread.
    bool run_raw(void (*job)(void*), void* context, bool wait = true) noexcept;
    template <class F> bool run(F& job) noexcept { return run_raw([](void* f) { (*static_cast<F*>(f))(); }, &job); }
    void stop_worker() noexcept;
    std::thread worker_;
    std::atomic<DWORD> worker_id_{0};
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    void (*job_)(void*) = nullptr;
    void* job_context_ = nullptr;
    bool stop_ = false, worker_failed_ = false;
    // Worker publishes the result with release; replay reads it with acquire.
    // The worker never writes shared Control caps or renderer/settings state.
    std::atomic<bool> init_done_{true};
    bool init_pending_ = false; // replay thread: result not published yet
    x64remote::Capabilities init_caps_{};
    bool initializing() const noexcept { return !init_done_.load(std::memory_order_acquire); }
    // The feature info NGX was initialised with, its path list and the folder
    // string it points at live as long as NGX: the SDK reserves InternalData
    // in it for NGX, and feature creation (first evaluate) searches the path
    // list for nvngx_dlss.dll. They were stack locals and a temporary.
    std::wstring folder_;
#ifdef EID3D11_EMBEDDED
    std::wstring data_folder_; // writable NGX logs/cache, separate from DLL search
#endif
    const wchar_t* paths_[1]{};
    NVSDK_NGX_FeatureCommonInfo info_{};
};

} // namespace remote
