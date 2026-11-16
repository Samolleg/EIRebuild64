#pragma once
#include "owned_resource_store.h"
#include <filesystem>
#include <cwchar>

namespace owned {
// Startup capture appends Base after the mod. Recognize both physical formats;
// the general ResourceStore still decides which sibling archive to open.
inline ResourceStoreConfig critter_base_mounts(const ResourceStoreConfig& mounts) {
    const auto last = [&](const wchar_t* family) {
        for (auto it = mounts.archives.rbegin(); it != mounts.archives.rend(); ++it) {
            const std::filesystem::path path(*it);
            const auto extension = path.extension().wstring();
            if ((_wcsicmp(extension.c_str(), L".res") == 0 || _wcsicmp(extension.c_str(), L".pak") == 0) &&
                _wcsicmp(path.stem().c_str(), family) == 0) return *it;
        }
        return std::wstring{};
    };
    ResourceStoreConfig result;
    const auto figures = last(L"figures"), textures = last(L"textures");
    if (!figures.empty() && !textures.empty()) result.archives = {figures, textures};
    return result;
}
}
