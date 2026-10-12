#include "../../stdafx.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "../../external/owned-renderer/Source/retained_static_scene.h"
#include <embedded_host.h>
#include "E1RendererFacade.h"
#include "E1Surface.h"
#include "E1SurfaceFacade.h"
#include <algorithm>
#include <cstdio>
#include <shellapi.h>

namespace {
const char* warpFailureStage = "unknown";

void mark(const char* value) {
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char* slash = strrchr(path, '\\');
    if (!slash || static_cast<std::size_t>(slash - path) + 32 >= MAX_PATH) return;
    strcpy_s(slash + 1, MAX_PATH - static_cast<std::size_t>(slash + 1 - path),
        "E1RendererFacadeCheck.log");
    if (HANDLE log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); log != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(log, value, static_cast<DWORD>(strlen(value)), &written, nullptr);
        WriteFile(log, "\r\n", 2, &written, nullptr);
        CloseHandle(log);
    }
}

struct WarpRendererBackend {
    owned::Renderer renderer;
    HRESULT ensure(HWND window, UINT width, UINT height) {
        try {
            const HRESULT result = renderer.ensure_for_test_warp(window, width, height);
            if (FAILED(result)) warpFailureStage = renderer.test_initialization_failure_stage();
            return result;
        } catch (const std::exception& error) {
            warpFailureStage = "renderer-ensure-exception";
            std::printf("Renderer ensure exception: %s\n", error.what());
            return E_FAIL;
        } catch (...) {
            warpFailureStage = "renderer-ensure-unknown-exception";
            return E_FAIL;
        }
    }
    HRESULT resize(UINT width, UINT height) { return renderer.resize(width, height); }
    void begin_scene() { renderer.begin_scene(); }
    void end_scene() { renderer.end_scene(); }
    HRESULT present() { return renderer.present(); }
};

struct PanelBlitSource {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    OwnedRender::E1Surface surface;
    std::vector<std::uint8_t> expected;
};

HRESULT create_panel_source(void* user, const owned::EmbeddedDeviceView& view) {
    auto& source = *static_cast<PanelBlitSource*>(user);
    source.device = view.device;
    source.context = view.context;
    constexpr UINT width = 4, height = 4;
    source.expected.resize(width * height * 4);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        const auto i = (y * width + x) * 4;
        source.expected[i + 0] = static_cast<std::uint8_t>(32 + x * 40);
        source.expected[i + 1] = static_cast<std::uint8_t>(48 + y * 36);
        source.expected[i + 2] = static_cast<std::uint8_t>(200 - x * 24 - y * 12);
        source.expected[i + 3] = 255;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = source.expected.data(); initial.SysMemPitch = width * 4;
    HRESULT hr = view.device->CreateTexture2D(&desc, &initial, &source.texture);
    if (FAILED(hr)) return hr;
    if (!source.surface.Complete(source.texture.Get())) return E_FAIL;
    owned::SurfaceImage first, cached;
    hr = OwnedRender::E1SurfaceImage(source.device.Get(), source.context.Get(),
        source.surface.Id(), first);
    if (FAILED(hr)) return hr;
    hr = OwnedRender::E1SurfaceImage(source.device.Get(), source.context.Get(),
        source.surface.Id(), cached);
    if (FAILED(hr)) return hr;
    const auto firstPixels = first.pixels.share();
    const auto cachedPixels = cached.pixels.share();
    if (!firstPixels || firstPixels != cachedPixels || first.id != source.surface.Id() ||
        first.generation == 0 || first.width != width || first.height != height ||
        firstPixels->size() != source.expected.size() ||
        !std::equal(firstPixels->begin(), firstPixels->end(), source.expected.begin()))
        return E_FAIL;
    const auto firstGeneration = first.generation;
    for (std::size_t i = 0; i < source.expected.size(); i += 4)
        source.expected[i + 1] ^= 0x7f;
    source.context->UpdateSubresource(source.texture.Get(), 0, nullptr,
        source.expected.data(), width * 4, 0);
    if (!source.surface.Complete(source.texture.Get())) return E_FAIL;
    owned::SurfaceImage refreshed;
    hr = OwnedRender::E1SurfaceImage(source.device.Get(), source.context.Get(),
        source.surface.Id(), refreshed);
    if (FAILED(hr)) return hr;
    const auto refreshedPixels = refreshed.pixels.share();
    if (!refreshedPixels || refreshed.generation <= firstGeneration ||
        refreshedPixels == firstPixels || refreshedPixels->size() != source.expected.size() ||
        !std::equal(refreshedPixels->begin(), refreshedPixels->end(), source.expected.begin()))
        return E_FAIL;
    return S_OK;
}

struct PanelBlitReadback {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const std::vector<std::uint8_t>* expected = nullptr;
    unsigned checked_pixels = 0;
};

HRESULT check_panel_output(void* user, ID3D11DeviceContext* context,
    ID3D11Texture2D* output) {
    auto& check = *static_cast<PanelBlitReadback*>(user);
    if (!output || context != check.context) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{}; output->GetDesc(&desc);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = check.device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) return hr;
    context->CopyResource(staging.Get(), output);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    bool match = true;
    for (UINT y = 0; y < 4; ++y) for (UINT x = 0; x < 4; ++x) {
        const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData) +
            std::size_t(10 + y) * mapped.RowPitch + (10 + x) * 4;
        const auto offset = (y * 4 + x) * 4;
        for (unsigned channel = 0; channel < 4; ++channel)
            match = match && pixel[channel] == (*check.expected)[offset + channel];
        ++check.checked_pixels;
    }
    context->Unmap(staging.Get(), 0);
    return match ? S_OK : E_FAIL;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int RunE1RendererFacadeWarpCheck() {
    mark("start");
    const bool shaderResourcePresent = FindResourceW(GetModuleHandleW(nullptr),
        L"EID3D11_SHADERS", MAKEINTRESOURCEW(10)) != nullptr;
    std::printf("WARP shader resource: %s\n", shaderResourcePresent ? "present" : "missing");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    wchar_t currentDirectory[MAX_PATH]{};
    const DWORD currentLength = GetCurrentDirectoryW(MAX_PATH, currentDirectory);
    if (!currentLength || currentLength >= MAX_PATH) {
        std::puts("Cannot resolve the Engine working directory");
        return 8;
    }
    const std::filesystem::path engine(currentDirectory);
    const std::filesystem::path gameRoot = engine.parent_path();
    const auto resources = gameRoot / L"Base" / L"Res";
    owned::EmbeddedHostPaths host;
    host.renderer_config = (std::filesystem::temp_directory_path() /
        (L"E1RendererFacadeWarp-" + std::to_wstring(GetCurrentProcessId()) + L".ini")).wstring();
    host.world_map_layouts = (gameRoot / L"MODS" / L"Dejavu" /
        L"world-map-layouts.json").wstring();
    host.mounts.loose_roots.push_back((gameRoot / L"Base" / L"Maps").wstring());
    host.mounts.archives = {(resources / L"figures.res").wstring(),
        (resources / L"menus.res").wstring(), (resources / L"textures.res").wstring()};
    for (const auto& archive : host.mounts.archives) {
        if (!std::filesystem::is_regular_file(archive)) {
            std::printf("Required base resource archive is missing: %ls\n", archive.c_str());
            return 9;
        }
    }
    try {
        owned::embedded_host_configuration().configure(std::move(host));
    } catch (const std::exception& error) {
        std::printf("Embedded host setup failed: %s\n", error.what());
        return 10;
    }
    WNDCLASSW cls{};
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"E1RendererFacadeWarpCheck";
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::printf("RegisterClassW failed: %lu\n", GetLastError());
        return 1;
    }
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"E1 WARP lifecycle",
        WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
    if (!window) {
        std::printf("CreateWindowExW failed: %lu\n", GetLastError());
        return 2;
    }
    mark("window-created");

    int result = 0;
    {
        OwnedRender::RendererFacadeCore<WarpRendererBackend> facade;
        mark("before-initialize");
        HRESULT hr = facade.Initialize(window, 320, 240);
        mark("after-initialize");
        if (FAILED(hr) || !facade.Ready()) {
            std::printf("WARP ensure failed at %s: 0x%08lx\n",
                warpFailureStage, static_cast<unsigned long>(hr));
            result = 3;
        } else if (FAILED(hr = facade.BeginFrame())) {
            std::printf("BeginFrame failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            result = 4;
        } else {
            PanelBlitSource source;
            hr = facade.Visit([&](WarpRendererBackend& backend) {
                return backend.renderer.visit_embedded_device(&source, &create_panel_source);
            });
            RECT destination{10, 10, 14, 14};
            if (SUCCEEDED(hr)) hr = OwnedRender::E1BlitPublishedSurface(facade,
                source.device.Get(), source.context.Get(), source.surface.Id(),
                destination, nullptr, nullptr, true);
            if (SUCCEEDED(hr)) hr = facade.Visit([&](WarpRendererBackend& backend) {
                hr = backend.renderer.flush_ui();
                if (FAILED(hr)) return hr;
                PanelBlitReadback readback{source.device.Get(), source.context.Get(),
                    &source.expected};
                hr = backend.renderer.visit_embedded_output(&readback, &check_panel_output);
                if (SUCCEEDED(hr) && readback.checked_pixels != 16) return E_FAIL;
                return hr;
            });
            if (FAILED(hr)) {
                std::printf("WARP published panel blit/readback failed: 0x%08lx\n",
                    static_cast<unsigned long>(hr));
                result = 8;
            } else {
                std::puts("WARP published 2D surface -> c56 blit -> output pixels PASS (16 pixels)");
            }
        }
        if (!result && FAILED(hr = facade.EndFrame())) {
            std::printf("EndFrame/Present failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            result = 5;
        } else if (!result && (FAILED(hr = facade.Resize(640, 360)) ||
                   facade.Width() != 640 || facade.Height() != 360)) {
            std::printf("Resize failed: 0x%08lx (%u x %u)\n",
                static_cast<unsigned long>(hr), facade.Width(), facade.Height());
            result = 6;
        } else if (!result && (FAILED(hr = facade.BeginFrame()) || FAILED(hr = facade.EndFrame()))) {
            std::printf("Resized frame failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            result = 7;
        }
        mark("lifecycle-finished");
    }
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, instance);
    if (!result) std::puts("E1RendererFacadeCheck: WARP ensure/resize/begin/end/present PASS");
    return result;
}

bool InspectRendererFacadeCheckCommandLine(int& result) {
    mark("inspect-entry");
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return false;
    const bool requested = count == 2 &&
        wcscmp(args[1], L"--e1-renderer-facade-warp-check") == 0;
    mark(requested ? "argument-matched" : "argument-unmatched");
    LocalFree(args);
    if (!requested) return false;
    result = RunE1RendererFacadeWarpCheck();
    return true;
}
