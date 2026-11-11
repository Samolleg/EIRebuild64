// Flip-model presenter in a child window; see d3d12_presenter.h.
#include "d3d12_presenter.h"

#include "d3d12_fidelityfx_runtime.h"
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>

#include <algorithm>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12 {

namespace {

constexpr UINT kDestroyWindow = WM_APP + 1;
constexpr const wchar_t* kClass = L"EID3D12CoreChild";

LRESULT CALLBACK child_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (message == kDestroyWindow) {
        DestroyWindow(window);
        return 0;
    }
    // Destroyed by us, or by the system with the game's window when the
    // game exits: either way the window thread ends.
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

} // namespace

Presenter::~Presenter() {
    release_swap();
    close_window();
}

// The swap chain and its buffers; the proxy's context first (SDK Shutdown:
// destroy the context, then release the last reference), then the waitable
// handle (each GetFrameLatencyWaitableObject handle is ours to close).
void Presenter::release_swap() noexcept {
    for (auto& buffer : buffers_) buffer.Reset();
    if (proxy_context_) {
        ffxContext context = static_cast<ffxContext>(proxy_context_);
        fidelityfx::api.destroy(&context, nullptr);
        proxy_context_ = nullptr;
    }
    // The last reference (the FidelityFX guide asserts a release count of 0
    // before a new swap chain is made on the window; each new one after a
    // presented one took ~5.02 s, run 941df5ae): logged when not the last.
    // Run 8b6ba2b4: one reference was left on every release. The frame
    // latency waitable object now closes first (it belongs to the swap
    // chain), and the line names the count held before the release.
    if (waitable_) { CloseHandle(waitable_); waitable_ = nullptr; }
    if (swap_) {
        swap_->AddRef();
        const ULONG held = swap_->Release();
        const ULONG left = swap_.Detach()->Release();
        if (left) remote::host_log("d3d12: the released swap chain still has %lu reference(s) (%lu held before the release)",
                                   static_cast<unsigned long>(left), static_cast<unsigned long>(held));
    }
}

// Ends the window thread: destroys the window if it still exists, else (the
// game's window took it down) makes sure the thread's loop has quit.
void Presenter::close_window() noexcept {
    if (!thread_.joinable()) return;
    if (!(window_ && IsWindow(window_) && PostMessageW(window_, kDestroyWindow, 0, 0)))
        PostThreadMessageW(GetThreadId(thread_.native_handle()), WM_QUIT, 0, 0);
    thread_.join();
    window_ = nullptr;
}

// The window's own thread: creating a child of the game's window attaches
// the input queues, so this thread only pumps messages and never waits on
// the GPU (as the D3D11 host's window thread).
void Presenter::window_thread(HANDLE ready) {
    WNDCLASSW type{};
    type.lpfnWndProc = child_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = kClass;
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    RECT client{};
    GetClientRect(parent_, &client);
    // Disabled child: mouse input over it reaches the game.
    window_ = CreateWindowExW(WS_EX_NOPARENTNOTIFY, kClass, L"EI D3D12 core", WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS | WS_VISIBLE,
                              0, 0, client.right, client.bottom, parent_, nullptr, type.hInstance, nullptr);
    SetEvent(ready);
    if (!window_) return;
    SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool Presenter::create(Core& core, HWND parent, std::string& reason, HWND adopt) {
    parent_ = parent;
    if (adopt) {
        window_ = adopt;
        adopted_ = true;
    } else {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready) { reason = hresult_text("CreateEvent (window thread)", HRESULT_FROM_WIN32(GetLastError())); return false; }
        thread_ = std::thread([this, ready] { window_thread(ready); });
        const bool started = WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0;
        CloseHandle(ready);
        if (!started || !window_) { reason = "the D3D12 child window could not be created in the game window"; return false; }
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = kBuffers;
    if (const HRESULT hr = core.device()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv_heap_)); FAILED(hr)) {
        reason = hresult_text("CreateDescriptorHeap (swap chain RTVs)", hr);
        return false;
    }
    rtv_start_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv_stride_ = core.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return create_swap(core, core.queue(), reason);
}

// The swap chain on `queue` (Present runs on the queue it was created with).
bool Presenter::create_swap(Core& core, ID3D12CommandQueue* queue, std::string& reason, bool proxy) {
    ComPtr<IDXGIFactory5> factory5;
    BOOL tearing = FALSE;
    if (SUCCEEDED(core.factory()->QueryInterface(IID_PPV_ARGS(&factory5))) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
        tearing_ = tearing != FALSE;
    RECT client{};
    GetClientRect(adopted_ ? window_ : parent_, &client);
    width_ = (std::max)(1L, client.right);
    height_ = (std::max)(1L, client.bottom);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = kFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBuffers;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    flags_ = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    desc.Flags = flags_;
    ComPtr<IDXGISwapChain1> swap;
    HRESULT hr = S_OK;
    if (proxy) {
        if (!fidelityfx::load(reason)) return false;
        // The window's only swap chain; the proxy owns the real one inside.
        ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
        version.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
        IDXGISwapChain4* created = nullptr;
        ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create{};
        create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
        create.header.pNext = &version.header;
        create.swapchain = &created;
        create.hwnd = window_;
        create.desc = &desc;
        create.fullscreenDesc = nullptr;
        create.dxgiFactory = core.factory();
        create.gameQueue = queue;
        ffxContext context = nullptr;
        const ffxReturnCode_t made = fidelityfx::api.create(&context, &create.header, nullptr);
        if (made != FFX_API_RETURN_OK || !created) {
            if (context) fidelityfx::api.destroy(&context, nullptr);
            reason = "the FidelityFX frame generation swap chain could not be created (ffx " + std::to_string(unsigned(made)) + ")";
            return false;
        }
        proxy_context_ = context;
        swap.Attach(created);
    } else {
        hr = core.factory()->CreateSwapChainForHwnd(queue, window_, &desc, nullptr, nullptr, &swap);
        if (FAILED(hr)) { reason = hresult_text("CreateSwapChainForHwnd (flip discard, child window)", hr); return false; }
    }
    if (FAILED(hr = swap.As(&swap_))) { reason = hresult_text("QueryInterface(IDXGISwapChain3)", hr); return false; }
    core.factory()->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    // Two frames queued at most: latency, not a frame cap (present interval 0).
    swap_->SetMaximumFrameLatency(2);
    waitable_ = swap_->GetFrameLatencyWaitableObject();
    return acquire_buffers(core, reason);
}

bool Presenter::move_to_queue(Core& core, ID3D12CommandQueue* queue, std::string& reason) {
    return timed_move(core, queue, reason, false);
}

bool Presenter::move_to_proxy(Core& core, std::string& reason) {
    return timed_move(core, core.queue(), reason, true);
}

bool Presenter::timed_move(Core& core, ID3D12CommandQueue* queue, std::string& reason, bool proxy) {
    LARGE_INTEGER frequency{}, t0{}, t1{}, t2{}, t3{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&t0);
    // Present/overlay work can follow the last frame-fence signal. Wait
    // for a fresh queue-tail signal before releasing any swap buffer.
    if (!core.wait_idle()) { reason = "the GPU did not go idle to move the swap chain"; return false; }
    QueryPerformanceCounter(&t1);
    release_swap();  // the window's only flip swap chain goes before the next is made
    QueryPerformanceCounter(&t2);
    const bool made = create_swap(core, queue, reason, proxy);
    QueryPerformanceCounter(&t3);
    const double to_ms = 1000.0 / double(frequency.QuadPart);
    std::snprintf(move_timing_, sizeof(move_timing_), "GPU wait %.1f, release %.1f, create %.1f", double(t1.QuadPart - t0.QuadPart) * to_ms,
                  double(t2.QuadPart - t1.QuadPart) * to_ms, double(t3.QuadPart - t2.QuadPart) * to_ms);
    return made;
}

bool Presenter::acquire_buffers(Core& core, std::string& reason) {
    for (UINT i = 0; i < kBuffers; ++i) {
        const HRESULT hr = swap_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i]));
        if (FAILED(hr)) { reason = hresult_text("IDXGISwapChain::GetBuffer", hr); return false; }
        buffers_[i]->SetName(L"eid3d11 D3D12 back buffer");
        core.device()->CreateRenderTargetView(buffers_[i].Get(), nullptr, {rtv_start_.ptr + std::size_t(i) * rtv_stride_});
    }
    return true;
}

bool Presenter::resize(Core& core, UINT width, UINT height, std::string& reason) {
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);
    if (width == width_ && height == height_) return true;
    before_resize();
    if (!core.wait_idle()) { reason = "the GPU did not go idle for a resize"; return false; }
    for (auto& buffer : buffers_) buffer.Reset();
    const HRESULT hr = swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags_);
    if (FAILED(hr)) { reason = hresult_text("IDXGISwapChain::ResizeBuffers", hr); return false; }
    width_ = width;
    height_ = height;
    return acquire_buffers(core, reason);
}

Presenter::State Presenter::prepare(Core& core, std::string& reason, bool wait_latency) {
    if (adopted_) {
        // The owner sizes and orders the window. Minimized: no wait, since
        // only a Present returns the frame latency count a wait takes.
        if (!IsWindow(parent_) || IsIconic(parent_)) return State::minimized;
        if (waitable_ && wait_latency) WaitForSingleObjectEx(waitable_, 1000, TRUE);
        return State::ready;
    }
    if (waitable_ && wait_latency) WaitForSingleObjectEx(waitable_, 1000, TRUE);
    if (!IsWindow(parent_)) return State::minimized;  // the game is closing
    RECT client{};
    GetClientRect(parent_, &client);
    if (client.right <= 0 || client.bottom <= 0 || IsIconic(parent_)) return State::minimized;
    const UINT width = static_cast<UINT>(client.right), height = static_cast<UINT>(client.bottom);
    if (width != width_ || height != height_) {
        SetWindowPos(window_, HWND_TOP, 0, 0, static_cast<int>(width), static_cast<int>(height), SWP_NOACTIVATE);
        before_resize();
        if (!core.wait_idle()) { reason = "the GPU did not go idle for a resize"; return State::failed; }
        for (auto& buffer : buffers_) buffer.Reset();
        const HRESULT hr = swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags_);
        if (FAILED(hr)) { reason = hresult_text("IDXGISwapChain::ResizeBuffers", hr); return State::failed; }
        width_ = width;
        height_ = height;
        if (!acquire_buffers(core, reason)) return State::failed;
    }
    // The D3D11 host raises its own child when it creates or resizes it;
    // this diagnostic window stays above it.
    if (GetWindow(parent_, GW_CHILD) != window_)
        SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return State::ready;
}

HRESULT Presenter::present(UINT sync_interval) noexcept {
    return swap_->Present(sync_interval, sync_interval == 0 && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
}

void Presenter::destroy(Core& core) noexcept {
    before_resize();
    core.wait_idle();
    release_swap();
    close_window();
}

} // namespace d3d12
