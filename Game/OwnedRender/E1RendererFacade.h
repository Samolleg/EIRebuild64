#pragma once
#include "../../external/owned-renderer/Source/frame_waits.h"

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "../../external/owned-renderer/Source/retained_static_scene.h"
#include "../../external/owned-renderer/Source/renderer.h"
#pragma pop_macro("max")
#pragma pop_macro("min")
#include <type_traits>
#include <utility>

namespace OwnedRender {

// Owns exactly one renderer instance and serializes its output/frame lifecycle.
// Kept independent of the current production client until the c56 menu path is
// ready to switch as one reviewed change.
template<class Backend>
class RendererFacadeCore {
    Backend renderer;
    HRESULT FinishSceneWork() noexcept {
        if constexpr (std::is_same_v<Backend, owned::Renderer>)
            return renderer.finish_embedded_scene_commands();
        return S_OK;
    }
    HWND window = nullptr;
    UINT outputWidth = 0;
    UINT outputHeight = 0;
    bool initialized = false;
    bool frameActive = false;

public:
    RendererFacadeCore() = default;
    bool D3D12Selected() const noexcept { return renderer.d3d12_selected(); }
    RendererFacadeCore(const RendererFacadeCore&) = delete;
    RendererFacadeCore& operator=(const RendererFacadeCore&) = delete;

    HRESULT Initialize(HWND target, UINT width, UINT height) noexcept {
        if (!target || !width || !height) return E_INVALIDARG;
        if (initialized) return E_UNEXPECTED;
        try {
            const HRESULT result = renderer.ensure(target, width, height);
            if (FAILED(result)) return result;
            window = target;
            outputWidth = width;
            outputHeight = height;
            initialized = true;
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT Resize(UINT width, UINT height) noexcept {
        if (!initialized) return E_UNEXPECTED;
        if (!width || !height) return E_INVALIDARG;
        if (frameActive) return DXGI_ERROR_INVALID_CALL;
        if (width == outputWidth && height == outputHeight) return S_FALSE;
        try {
            const HRESULT work = FinishSceneWork();
            if (FAILED(work)) return work;
            const HRESULT result = renderer.resize(width, height);
            if (SUCCEEDED(result)) {
                outputWidth = width;
                outputHeight = height;
            }
            return result;
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT BeginFrame() noexcept {
        if (!initialized) return E_UNEXPECTED;
        if (frameActive) return DXGI_ERROR_INVALID_CALL;
        try {
            const owned::frame_waits::Scope record(owned::frame_waits::record);
            const HRESULT work = FinishSceneWork();
            if (FAILED(work)) return work;
            renderer.begin_scene();
            frameActive = true;
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT EndFrame() noexcept {
        if (!initialized || !frameActive) return E_UNEXPECTED;
        frameActive = false;
        try {
            const owned::frame_waits::Scope record(owned::frame_waits::record);
            if constexpr (std::is_same_v<Backend, owned::Renderer>) {
                const HRESULT hr = renderer.queue_embedded_batch(owned::Renderer::EmbeddedBatch::end_scene);
                if (FAILED(hr)) return hr;
            } else renderer.end_scene();
            return renderer.present();
        } catch (...) {
            return E_FAIL;
        }
    }

    // Close an off-screen/capture frame without presenting it. DrawFrame(false)
    // uses this path while it reads the rendered backbuffer into a surface.
    HRESULT DiscardFrame() noexcept {
        if (!initialized || !frameActive) return E_UNEXPECTED;
        frameActive = false;
        try {
            const owned::frame_waits::Scope record(owned::frame_waits::record);
            const HRESULT work = FinishSceneWork();
            renderer.end_scene();
            return work;
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT QueueWorldDraw(owned::Renderer::EmbeddedDraw draw, bool startWorld = false) {
        if (!initialized || !frameActive) return E_UNEXPECTED;
        return renderer.queue_embedded_draw(std::move(draw), startWorld);
    }
    bool OutputPending() const noexcept { return renderer.embedded_output_pending(); }
    bool OutputExecuting() const noexcept { return OutputPending() && !renderer.embedded_worker_idle(); }
    HRESULT BeginOverlapSimulation() { return renderer.begin_embedded_simulation(); }
    void FinishOverlapSimulation() { renderer.finish_embedded_frame(); }
    HRESULT FinishWorldDraw() noexcept { return FinishSceneWork(); }

    HRESULT SubmitWorldBatch(owned::Renderer::EmbeddedBatch batch) {
        if (!initialized || !frameActive) return E_UNEXPECTED;
        return renderer.queue_embedded_batch(batch);
    }

    bool Ready() const noexcept { return initialized; }
    bool FrameActive() const noexcept { return frameActive; }
    HWND Window() const noexcept { return window; }
    UINT Width() const noexcept { return outputWidth; }
    UINT Height() const noexcept { return outputHeight; }

    template<class Visitor>
    HRESULT Visit(Visitor&& visitor) {
        if (!initialized) return E_UNEXPECTED;
        try {
            const owned::frame_waits::Scope record(owned::frame_waits::record);
            const HRESULT work = FinishSceneWork();
            if (FAILED(work)) return work;
            return visitor(renderer);
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT VisitDevice(void* user, owned::EmbeddedDeviceVisitor visitor) {
        return Visit([&](Backend& value) {
            return value.visit_embedded_device(user, visitor);
        });
    }

    HRESULT VisitOutput(void* user, owned::EmbeddedOutputVisitor visitor) {
        return Visit([&](Backend& value) {
            return value.visit_embedded_output(user, visitor);
        });
    }

    // Native figure adapters submit their completed, pointer-bounded snapshot
    // during the active c56 frame. The renderer copies the geometry into its
    // own queue before this call returns; callers must not retain engine data.
    HRESULT QueueNative(const owned::NativeGeometryView& view,
                        const owned::FixedState& state,
                        owned::SurfaceImage* texture,
                        owned::NativeGeometryResult* result = nullptr) {
        if (!initialized || !frameActive) return E_UNEXPECTED;
        return Visit([&](Backend& value) {
            return value.queue_native(view, state, texture, result);
        });
    }
    HRESULT QueueNativeDirect(const owned::NativeGeometryView& view,
                              const owned::FixedState& state,
                              ID3D11ShaderResourceView* texture,
                              owned::NativeGeometryResult* result = nullptr) {
        if (!initialized || !frameActive) return E_UNEXPECTED;
        return Visit([&](Backend& value) {
            return value.queue_native_direct(view, state, texture, result);
        });
    }
};

using E1RendererFacade = RendererFacadeCore<owned::Renderer>;
static_assert(std::is_same_v<decltype(std::declval<owned::Renderer&>().ensure(
    HWND{}, UINT{}, UINT{})), HRESULT>);
static_assert(std::is_same_v<decltype(std::declval<owned::Renderer&>().resize(
    UINT{}, UINT{})), HRESULT>);
static_assert(std::is_same_v<decltype(std::declval<owned::Renderer&>().begin_scene()), void>);
static_assert(std::is_same_v<decltype(std::declval<owned::Renderer&>().end_scene()), void>);
static_assert(std::is_same_v<decltype(std::declval<owned::Renderer&>().present()), HRESULT>);

} // namespace OwnedRender
