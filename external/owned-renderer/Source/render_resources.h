#pragma once
#include <windows.h>
#include <cwchar>

namespace owned {
// Resource pointers retained by the renderer live until process exit.
inline HMODULE render_resource_module() noexcept {
    static const HMODULE module = []() noexcept -> HMODULE {
        wchar_t path[32768];
        const DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
        if (!length || length >= _countof(path)) return nullptr;
        wchar_t* slash = std::wcsrchr(path, L'\\');
        if (!slash) return nullptr;
        wchar_t* filename = slash + 1;
        constexpr wchar_t name[] = L"EI.RenderResources.dll";
        if (_countof(path) - (filename - path) < _countof(name)) return nullptr;
        std::wmemcpy(filename, name, _countof(name));
        return LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    }();
    return module;
}
}
