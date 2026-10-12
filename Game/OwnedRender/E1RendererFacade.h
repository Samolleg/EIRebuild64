#pragma once

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
    HWND window = nullptr;
    UINT outputWidth = 0;
    UINT outputHeight = 0;
    bool initialized = false;
    bool frameActive = false;

public:
    RendererFacadeCore() = default;
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
            renderer.end_scene();
            return renderer.present();
        } catch (...) {
            return E_FAIL;
        }
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
