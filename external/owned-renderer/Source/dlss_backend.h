#pragma once
// Renderer-owned NGX lifecycle, adapted from upstream Host/dlss.cpp.
// No remote protocol or resource IDs. Calls are serialized by the renderer.
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace owned {
// NGX API order, distinct from the settings quality order (dlss_ngx_mode).
enum class DlssQuality { performance, balanced, quality, ultra_performance, dlaa };
struct DlssCapability {
    bool available = false;
    bool needs_updated_driver = false;
    bool needs_updated_driver_known = false;
    bool feature_init_result_known = false;
    std::uint32_t feature_init_result = 0;
    std::uint32_t result = 0;
    char reason[192] = "NGX not initialized";
};
struct DlssShape {
    unsigned render_width = 0, render_height = 0, output_width = 0, output_height = 0;
    DlssQuality quality = DlssQuality::quality;
    bool hdr = false, depth_inverted = false, auto_exposure = true;
    bool operator==(const DlssShape& b) const noexcept {
        return render_width == b.render_width && render_height == b.render_height &&
            output_width == b.output_width && output_height == b.output_height &&
            quality == b.quality && hdr == b.hdr && depth_inverted == b.depth_inverted &&
            auto_exposure == b.auto_exposure;
    }
};
// Native resources shared with FSR 2. Motion is current -> previous in render
// pixels, without jitter; jitter is the applied screen displacement, right/down.
struct DlssFrame {
    DlssShape shape{};
    ID3D11Resource* color = nullptr;
    ID3D11Resource* depth = nullptr;
    ID3D11Resource* motion = nullptr;
    ID3D11Resource* output = nullptr;
    ID3D11Resource* exposure = nullptr;
    float jitter_x = 0, jitter_y = 0;
    float mv_scale_x = 1, mv_scale_y = 1;
    bool reset = true;
};
class DlssBackend {
public:
    using Log = void (*)(const char*);
    DlssBackend() = default;
    ~DlssBackend() { shutdown(); }
    DlssBackend(const DlssBackend&) = delete;
    DlssBackend& operator=(const DlssBackend&) = delete;
    // The actual native device; DLL beside EXE, writable NGX data in LocalAppData.
    bool initialize(ID3D11Device* device, Log log) noexcept;
    const DlssCapability& capability() const noexcept { return capability_; }
    const char* last_error() const noexcept { return error_; }
    bool optimal(unsigned output_width, unsigned output_height, DlssQuality quality,
                 unsigned& render_width, unsigned& render_height) noexcept;
    bool create_feature(ID3D11DeviceContext* context, const DlssShape& shape) noexcept;
    void release_feature() noexcept;
    void device_lost(HRESULT reason) noexcept;
    void shutdown() noexcept;
    bool evaluate(ID3D11DeviceContext* context, const DlssFrame& frame) noexcept;
private:
    void cleanup() noexcept;
    bool fail(const char* operation, std::uint32_t result) noexcept;
    bool device_ready() noexcept;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    NVSDK_NGX_Parameter* capabilities_ = nullptr;
    NVSDK_NGX_Parameter* parameters_ = nullptr;
    NVSDK_NGX_Handle* feature_ = nullptr;
    bool initialized_ = false;
    DlssShape shape_{};
    DlssCapability capability_{};
    char error_[192]{};
    Log log_ = nullptr;
};
} // namespace owned
