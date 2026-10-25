#pragma once
#include "../RuntimePaths64.h"
#include "ResourceBootstrap.h"
#include <filesystem>
#include <array>

namespace OwnedRender {
inline std::filesystem::path ResolveProfilePath(const std::filesystem::path& value,
    const std::filesystem::path& profileDirectory) {
    if (value.empty() || value.wstring().find(L'\0') != std::wstring::npos ||
        !profileDirectory.is_absolute())
        throw std::invalid_argument("invalid renderer profile path");
    // Drive-relative and root-relative paths would depend on process state.
    if (!value.is_absolute() && (value.has_root_name() || value.has_root_directory()))
        throw std::invalid_argument("ambiguous renderer profile path");
    return (value.is_absolute() ? value : profileDirectory / value).lexically_normal();
}

// Package is CModPackage in production. Copy its already selected resource
// plan, including duplicates and the ignore-original flag; never enumerate RES.
template<class Package>
owned::EmbeddedHostPaths CaptureStartupPaths(const std::filesystem::path& engine,
    const std::filesystem::path& profile, const std::filesystem::path& work,
    const std::filesystem::path& resources, const Package* package) {
    const auto profileDirectory = profile.parent_path();
    const auto workRoot = ResolveProfilePath(work, profileDirectory);
    const auto resourceRoot = ResolveProfilePath(resources, profileDirectory);
    owned::EmbeddedHostPaths paths;
    if (!engine.is_absolute()) throw std::invalid_argument("relative engine directory");
    paths.renderer_config = RuntimePaths64::Config("rddraw.ini").wstring();
    const auto modRoot = package ? ResolveProfilePath(std::filesystem::path(package->Directory()),
        profileDirectory) : workRoot;
    paths.world_map_layouts = (modRoot / L"world-map-layouts.json").wstring();
    if (package) paths.mounts.loose_roots.push_back((modRoot / L"Maps").wstring());
    paths.mounts.loose_roots.push_back((workRoot / L"Maps").wstring());
    const char* categories[] = {"figures.res", "menus.res", "textures.res", "redress.res"};
    std::array<bool, 4> ignore{};
    for (size_t i = 0; i != ignore.size(); ++i) {
        std::vector<std::string> replacements;
        if (package) package->ResourcePlan(categories[i], replacements, ignore[i]);
        for (const auto& replacement : replacements)
            paths.mounts.archives.push_back(ResolveProfilePath(
                std::filesystem::path(replacement), profileDirectory).wstring());
    }
    // All selected overrides precede base archives, as in candidate-56.
    // Include menus.res for menu figure assets, just as the host figure chain.
    for (size_t i = 0; i != ignore.size(); ++i)
        if (!ignore[i]) paths.mounts.archives.push_back((resourceRoot / categories[i]).wstring());
    return paths;
}
}
