#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>

// EIRebuild test isolation: retail ASI patchers must not be scanned as codecs.
inline std::string EIRebuildMilesCodecDirectory()
{
    char module[MAX_PATH];
    DWORD size = GetModuleFileNameA(NULL, module, MAX_PATH);
    if (!size || size >= MAX_PATH)
        throw std::runtime_error("Cannot locate EIRebuild Miles codecs");
    std::string path(module, size);
    path.resize(path.find_last_of("\\/") + 1);
    path += "EIRebuild.Miles";
    DWORD attrs = GetFileAttributesA((path + "\\mp3dec.asi").c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Missing EIRebuild.Miles/mp3dec.asi");
    return path;
}
