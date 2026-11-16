#pragma once
#include "../Pak/Pak.h"

namespace InstalledGameData {
inline std::filesystem::path FindRoot(const std::filesystem::path& engine)
{
    const auto complete = [](const std::filesystem::path& root) {
        return std::filesystem::is_regular_file(ei::pak::ResolveArchivePath(root / "Res" / "database.res")) &&
            std::filesystem::is_directory(root / "Maps");
    };
    const auto portable = engine.parent_path() / "Base";
    if (complete(portable)) return portable;
    const auto classic = engine.parent_path().parent_path();
    return complete(classic) ? classic : std::filesystem::path{};
}
}
