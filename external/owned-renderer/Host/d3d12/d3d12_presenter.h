#pragma once
// Presenter of the D3D12 backend (plan 2.5, section 4 item 11): a
// flip-model swap chain on the direct queue in a disabled child window of
// the game window (mouse input passes through to the game), following the
// game window's client size. Present, resize and frame latency live here.
#include "d3d12_core.h"

#include <cstdint>
#include <string>
#include <thread>

namespace d3d12 {

class Presenter {
public:
    static constexpr UINT kBuffers = 3;
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    ~Presenter();
    // The child window (on its own message thread, as the D3D11 host's) over
    // parent's client area, and the swap chain. adopt: present into this
    // existing child instead (the core-mode host's, sized by its Resize
    // command through resize(); no window of its own, no automatic resize).
    bool create(Core& core, HWND parent, std::string& reason, HWND adopt = nullptr);
    // The adopted window's new client size: waits for the GPU and resizes.
    bool resize(Core& core, UINT width, UINT height, std::string& reason);
    // Before the frame: waits on the frame latency object, follows the
    // parent's client size (waits for the GPU and resizes), keeps the window
    // above its siblings. minimized: nothing to draw this time.
    // wait_latency false: the frame generation pacer presents and waits on
    // the latency object itself (before_resize drains it first).
    enum class State { ready, minimized, failed };
    State prepare(Core& core, std::string& reason, bool wait_latency = true);
    // Replaces the swap chain with a plain one on the requested queue.
    // The GPU and the pacer must be idle; plain/DLSS-G toggles keep it.
    bool move_to_queue(Core& core, ID3D12CommandQueue* queue, std::string& reason);
    // FSR frame generation: the swap chain recreated as the FidelityFX proxy
    // (FOR_HWND, on the core queue; SDK 2.3.0 frame-interpolation-swap-chain.md),
    // which interpolates and paces the presents on its own threads. Leave it
    // with move_to_queue (the frame generation context destroyed first). The
    // main queue's tail is waited before its buffers are released.
    bool move_to_proxy(Core& core, std::string& reason);
    // The last move's step times (ms): GPU wait, old swap chain released, new one made.
    const char* move_timing() const noexcept { return move_timing_; }
    bool proxy() const noexcept { return proxy_context_ != nullptr; }
    IDXGISwapChain* swap_chain() const noexcept { return swap_.Get(); }
    // The frame latency wait the pacer takes before each Present.
    void wait_frame_latency() noexcept { if (waitable_) WaitForSingleObjectEx(waitable_, 1000, TRUE); }
    // Runs before any resize or destroy (the pacer drains its presents).
    void set_before_resize(void (*hook)(void*), void* context) noexcept { before_resize_ = hook; before_resize_context_ = context; }
    // After Core::end_frame. Interval 0 tears where the display allows it.
    HRESULT present(UINT sync_interval = 0) noexcept;
    // Waits for the GPU and releases the swap chain and window.
    void destroy(Core& core) noexcept;

    ID3D12Resource* back_buffer() const noexcept { return buffers_[swap_->GetCurrentBackBufferIndex()].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv() const noexcept {
        return {rtv_start_.ptr + std::size_t(swap_->GetCurrentBackBufferIndex()) * rtv_stride_};
    }
    UINT width() const noexcept { return width_; }
    UINT height() const noexcept { return height_; }
    HWND window() const noexcept { return window_; }

private:
    char move_timing_[96] = {};
    bool timed_move(Core& core, ID3D12CommandQueue* queue, std::string& reason, bool proxy);
    bool acquire_buffers(Core& core, std::string& reason);
    bool create_swap(Core& core, ID3D12CommandQueue* queue, std::string& reason, bool proxy = false);
    void release_swap() noexcept;
    void before_resize() noexcept { if (before_resize_) before_resize_(before_resize_context_); }
    void (*before_resize_)(void*) = nullptr;
    void* before_resize_context_ = nullptr;
    void window_thread(HANDLE ready);
    void close_window() noexcept;

    HWND parent_ = nullptr, window_ = nullptr;
    std::thread thread_;
    ComPtr<IDXGISwapChain3> swap_;
    void* proxy_context_ = nullptr;  // ffxContext of the FidelityFX proxy swap chain
    HANDLE waitable_ = nullptr;
    UINT flags_ = 0, width_ = 0, height_ = 0;
    bool tearing_ = false, adopted_ = false;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_start_{};
    UINT rtv_stride_ = 0;
    ComPtr<ID3D12Resource> buffers_[kBuffers];
};

} // namespace d3d12
