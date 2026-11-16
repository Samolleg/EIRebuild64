#pragma once
#include "owned_resource_store.h"
#include "../../../Pak/Pak.h"

namespace owned {
// Identity gate only: the actual readers retain their configured mount order.
inline bool neighbour_profile_matches(const ResourceStoreConfig& mounts, const std::wstring& layout)
{
    for (const auto& path : mounts.archives) {
        const auto name = std::filesystem::path(path).filename().wstring();
        if (_wcsicmp(name.c_str(), L"databasejmv.res") == 0 ||
            _wcsicmp(name.c_str(), L"databasejmv.pak") == 0) return true;
    }
    // The database need not be listed among the visual archives.
    const auto file = std::filesystem::path(layout);
    if (file.empty() || !file.is_absolute()) return false;
    const auto database = ei::pak::ResolveArchivePath(file.parent_path() / "res" / "databaseJmv.res");
    const auto attributes = GetFileAttributesW(database.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
}
