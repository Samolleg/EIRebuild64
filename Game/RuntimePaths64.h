#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include "AgentBackground.h"

// Writable paths belong to the x64 installation, never to WorkPath or the mod.
// Supported layout: <game>/Engine64/EIRebuild64.exe.
namespace RuntimePaths64 {
namespace fs = std::filesystem;
inline const fs::path& Root() {
    static const fs::path root = [] {
        wchar_t module[32768]{};
        const DWORD size = GetModuleFileNameW(nullptr, module, 32768);
        if (!size || size >= 32768) throw std::runtime_error("Cannot resolve x64 game root");
        return fs::path(module).parent_path().parent_path();
    }();
    return root;
}
inline fs::path Directory(const wchar_t* name) {
    auto path = Root() / name;
    // Config64 remains a read-only input in unattended runs, including absent
    // subdirectories. Logs, cache and MCP save slots keep their normal roots.
    if (!AgentBackground::Enabled() || (wcscmp(name,L"Config64")!=0 && wcsncmp(name,L"Config64/",9)!=0))
        fs::create_directories(path);
    return path;
}
inline fs::path Config(const char* name) { return Directory(L"Config64") / name; }
inline std::string ConfigRead(const char* name, const char* initial) {
    const auto path = Config(name);
    return fs::exists(path) ? path.string() : std::string(initial);
}
inline fs::path Log(const char* name) { return Directory(L"Logs64") / name; }
inline const std::string& Textures() {
    static const auto path = Directory(L"Cache64/textures").string() + "\\";
    return path;
}
inline const std::string& Camera() {
    static const auto path = Directory(L"Config64/camera").string() + "\\";
    return path;
}
}
