#pragma once
#include <windows.h>
#include <string>

namespace owned {
#if defined(EID3D11_EMBEDDED) && defined(_WIN64)
inline constexpr const char* debug_output_environment = "EI_RENDERER64_DEBUG_DIR";
inline constexpr const wchar_t* debug_output_environment_w = L"EI_RENDERER64_DEBUG_DIR";
inline constexpr const char* debug_output_suffix = "\\EID3D11-x64";
inline constexpr const char* debug_output_fallback = ".\\EID3D11-x64-debug";
#else
inline constexpr const char* debug_output_environment = "EI_RENDERER_DEBUG_DIR";
inline constexpr const wchar_t* debug_output_environment_w = L"EI_RENDERER_DEBUG_DIR";
inline constexpr const char* debug_output_suffix = "\\EID3D11";
inline constexpr const char* debug_output_fallback = ".\\EID3D11-debug";
#endif
inline std::string debug_output_root() {
    char path[MAX_PATH]{};
    DWORD size = GetEnvironmentVariableA(debug_output_environment, path, MAX_PATH);
    if (size && size < MAX_PATH) return path;
    size = GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH);
    return size && size < MAX_PATH ? std::string(path) + debug_output_suffix
                                 : debug_output_fallback;
}
}
