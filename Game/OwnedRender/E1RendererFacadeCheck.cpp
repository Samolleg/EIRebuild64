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
        } else if (FAILED(hr = facade.EndFrame())) {
            std::printf("EndFrame/Present failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            result = 5;
        } else if (FAILED(hr = facade.Resize(640, 360)) ||
                   facade.Width() != 640 || facade.Height() != 360) {
            std::printf("Resize failed: 0x%08lx (%u x %u)\n",
                static_cast<unsigned long>(hr), facade.Width(), facade.Height());
            result = 6;
        } else if (FAILED(hr = facade.BeginFrame()) || FAILED(hr = facade.EndFrame())) {
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
