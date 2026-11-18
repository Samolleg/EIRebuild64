#pragma once
// FSR 3.1 frame generation (frame_generation 2; FidelityFX SDK 2.4.0, the
// signed amd_fidelityfx_loader_dx12.dll and amd_fidelityfx_framegeneration_dx12.dll
// beside the host exe, delay-loaded). The FidelityFX proxy swap chain
// (Presenter::move_to_proxy) interpolates and paces the presents on its own
// threads (SDK frame-interpolation-swap-chain.md); this owns the frame
// generation context: per real frame a prepare on the frame's list (depth,
// motion, camera) and a configure before Present with the same frame id and
// the HUD-less image (UI mode "HUD-less": the SDK tells the interface from
// the world by comparing it with the back buffer). allowAsyncWorkloads is
// off, so interpolation runs on the game queue and the HUD-less image may be
// rewritten by the next frame (frame-interpolation-api.md, resource lifetime).
#include "d3d12_core.h"
#include "d3d12_timestamps.h"

#include <cstdint>
#include <string>
#include <vector>

namespace d3d12 {

class FsrFrameGeneration {
public:
    // One prepare: render size, jitter (render pixels), camera.
    struct Prepare {
        UINT render_w = 0, render_h = 0;
        float jitter_x = 0, jitter_y = 0;
        float frame_ms = 0;
        bool reset = false;
        float near_plane = 0, far_plane = 0, fov_y = 0;
        float position[3]{}, up[3]{}, right[3]{}, forward[3]{};
    };
    ~FsrFrameGeneration() { destroy(nullptr); }
    // Whether the FidelityFX DLLs load and offer frame generation (once; the
    // reason otherwise).
    static bool available(ID3D12Device* device, std::string& reason) noexcept;
    bool created() const noexcept { return context_ != nullptr; }
    UINT width() const noexcept { return width_; }
    UINT height() const noexcept { return height_; }
    // The context for the display size (the swap chain's).
    bool create(ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height, DXGI_FORMAT format, std::string& reason) noexcept;
    // Turns generation off on the swap chain (waits for its presents) and
    // destroys the context (SDK Shutdown order: before the swap chain context).
    void destroy(IDXGISwapChain* swap) noexcept;
    // The prepare for the next frame id on `list`: depth (D24S8 or D32,
    // shader read) and motion (render pixels, current to previous).
    bool prepare(ID3D12GraphicsCommandList* list, ID3D12Resource* depth, ID3D12Resource* motion, const Prepare& p, std::string& error) noexcept;
    // Before Present, the same frame id: generation on when this frame was
    // prepared and is a game frame (hudless in shader-read state), else off.
    bool configure(IDXGISwapChain* swap, ID3D12Resource* hudless, std::string& error) noexcept;
    // VRAM of the context (bytes, 0: unknown).
    std::uint64_t vram() const noexcept;
    // The last configure turned generation on (the frame is interpolated).
    bool generating() const noexcept { return enabled_; }
    // Interpolation workloads the proxy asked for so far (its callback): the
    // generated frames, independent of DXGI's present statistics.
    static std::uint64_t dispatches() noexcept;
    // SDK callback and post-Present submission boundary, on the recording thread.
    unsigned dispatch_generation(void* params) noexcept;
    void submitted() noexcept;
    // Resolved external FG pass, independent of the ordinary frame-list sum.
    static std::string take_gpu_report();

private:
    void collect_gpu_times() noexcept;
    Timestamps gpu_times_;
    ComPtr<ID3D12CommandQueue> gpu_queue_;
    ComPtr<ID3D12Fence> gpu_fence_;
    std::uint64_t gpu_values_[Core::kFramesInFlight]{};
    std::uint64_t gpu_serial_ = 0;
    unsigned gpu_slot_ = 0;
    bool gpu_recorded_ = false;
    void* context_ = nullptr;  // ffxContext
    UINT width_ = 0, height_ = 0;
    std::uint64_t frame_id_ = 0;
    bool prepared_ = false;  // this frame id has a prepare
    bool enabled_ = false;   // the last configure turned generation on
};

// FSR 3.1 upscaling (upscaler 2 on D3D12; the same SDK, plus the signed
// amd_fidelityfx_upscaler_dx12.dll beside the host exe). One context for the
// output size, recreated when it changes. A replaced or released context is
// retired and destroyed only once the fence passed its last dispatch, so two
// may be alive for the frames in flight (each holds its own resources;
// ffx-api.md). Inputs as for FSR
// frame generation (depth D24S8 or D32, motion in render pixels, current to
// previous, unjittered), LDR colour (exposure 1), optional R8 reactive and
// transparency-and-composition masks; the output is an unordered-access image.
class FsrUpscaler {
public:
    struct Dispatch {
        UINT render_w = 0, render_h = 0, out_w = 0, out_h = 0;
        float jitter_x = 0, jitter_y = 0;  // render pixels
        float frame_ms = 0, sharpness = 0;
        bool reset = false;
        float near_plane = 0, far_plane = 0, fov_y = 0;
        std::uint64_t recorded = 0;  // the fence value of the list recorded into
    };
    ~FsrUpscaler() { destroy(); }
    // Whether the FidelityFX DLLs load and offer upscaling (once; the reason otherwise).
    static bool available(ID3D12Device* device, std::string& reason) noexcept;
    // resources: colour, depth, motion, output, reactive (unordered access),
    // transparency, then the world colour before and after the transparent
    // effects. With reactive and both world colours the SDK generates the
    // reactive mask first; any of them null: no mask. Creates the context first when needed.
    bool dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources, const Dispatch& d, std::string& error) noexcept;
    void destroy() noexcept;  // the current context, at once
    // Moves the current context to the retired list with the fence value of
    // its last dispatch; the next dispatch creates a new one.
    void retire() noexcept;
    // Destroys the retired contexts whose last dispatch the fence passed
    // (completed), or (teardown, GPU idle) whose list was never submitted
    // (recorded at or after unsubmitted). Returns how many.
    unsigned collect(std::uint64_t completed, std::uint64_t unsubmitted = ~0ull) noexcept;
    // Forgets the current and retired contexts without destroying them
    // (teardown with their GPU work not confirmed finished: the process exit frees them).
    void abandon() noexcept { context_ = nullptr; retired_.clear(); }
    bool created() const noexcept { return context_ != nullptr; }
    std::size_t retired() const noexcept { return retired_.size(); }
    std::uint64_t recorded() const noexcept { return recorded_; }  // the fence value of the current context's last dispatch
    std::uint64_t vram() const noexcept;          // the current context
    std::uint64_t retired_vram() const noexcept;  // the retired contexts

private:
    struct Retired {
        void* context;           // ffxContext
        std::uint64_t recorded;  // the fence value of its last dispatch
    };
    void* context_ = nullptr;  // ffxContext
    std::uint64_t recorded_ = 0;
    std::vector<Retired> retired_;
    UINT render_w_ = 0, render_h_ = 0, out_w_ = 0, out_h_ = 0;
};

} // namespace d3d12
