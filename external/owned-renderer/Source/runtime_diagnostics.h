#pragma once
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <cwchar>

// Immutable launch policy shared by the game and embedded renderer.
namespace RuntimeDiagnostics {
inline bool Enabled() noexcept {
    static const bool enabled = []() noexcept {
        int count = 0;
        auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) ExitProcess(ERROR_NOT_ENOUGH_MEMORY);
        bool found = false;
        for (int i = 1; i < count; ++i) {
            if (std::wcscmp(arguments[i], L"--background") == 0 ||
                std::wcscmp(arguments[i], L"--diagnostics") == 0) {
                found = true;
                break;
            }
        }
        LocalFree(arguments);
        return found;
    }();
    return enabled;
}
}
