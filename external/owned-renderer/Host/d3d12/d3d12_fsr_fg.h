#pragma once
// FSR 3.1 frame generation (frame_generation 2; FidelityFX SDK 2.3.0, the
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

#include <cstdint>
#include <string>

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
    bool create(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format, std::string& reason) noexcept;
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

private:
    void* context_ = nullptr;  // ffxContext
    UINT width_ = 0, height_ = 0;
    std::uint64_t frame_id_ = 0;
    bool prepared_ = false;  // this frame id has a prepare
    bool enabled_ = false;   // the last configure turned generation on
};

} // namespace d3d12
