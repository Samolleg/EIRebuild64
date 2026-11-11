#pragma once
// Local ownership of the author's executor and NGX worker. No Host process,
// shared-memory control block, replay stream or D3D11 device is involved.
#include "d3d12_frame.h"
#include "x64_remote_link.h" // value-only SR/FG descriptors shared with author

namespace owned {

class EmbeddedD3D12 {
public:
    EmbeddedD3D12();
    ~EmbeddedD3D12();
    EmbeddedD3D12(const EmbeddedD3D12&) = delete;
    EmbeddedD3D12& operator=(const EmbeddedD3D12&) = delete;
    // The caller owns the window; this object owns all GPU execution.
    // Failure leaves no executor and supplies a printable reason, never D3D11.
    bool initialize(HWND window, std::string& reason) noexcept;
    D3D12Frame* frame() const noexcept;
    // Serialized frame owner only: before input/simulation, before drawing,
    // and after Renderer::present_frame, including failed/omitted presents.
    // Replace core_host's game-release and present-record boundaries.
    HRESULT begin_simulation(int reflex_mode) noexcept;
    HRESULT begin_render() noexcept;
    void finish_render() noexcept;
    bool dlss_caps(x64remote::DlssCaps& out) const noexcept;
    bool dlss_size(UINT width, UINT height, int mode, UINT& w, UINT& h) noexcept;
    HRESULT dlss_evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources,
                          const x64remote::DlssEvaluate& evaluate) noexcept;
    void dlss_release() noexcept;
    bool fg_caps(x64remote::DlssgCaps& out) noexcept;
    HRESULT fg_evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* const* resources,
                        const x64remote::DlssgEvaluate& evaluate) noexcept;
    void fg_release() noexcept;
    std::uint64_t fg_vram(UINT width, UINT height, UINT render_w, UINT render_h) noexcept;
    const char* failure() const noexcept;
    HRESULT device_removed_reason() const noexcept;
    // Before Renderer releases resources: stop NGX while its GPU wait still
    // has a live executor. shutdown() follows after all renderer textures,
    // buffers, upload jobs, overlay and profiler have been retired.
    // External snapshots may outlive shutdown for release only. Their old
    // resources must not be submitted to a replacement executor/device.
    void stop_ngx() noexcept;
    void shutdown() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace owned
