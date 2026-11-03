#pragma once
#include "../RuntimePaths64.h"
#include "ResourceBootstrap.h"
#include <filesystem>
#include <array>

namespace OwnedRender {
// Same ordered figure/texture chains consumed by the renderer and source managers.
// ResourcePlan already contains the selected mod and ignore-original decisions.
template<class Package, class Resolve>
void CaptureGraphicsMounts(owned::ResourceStoreConfig& mounts,
    const std::filesystem::path& resourceRoot, const Package* package, Resolve resolve) {
    const char* categories[] = {"figures.res", "textures.res", "redress.res", "menus.res"};
    std::array<bool, 4> ignore{};
    std::array<std::vector<std::string>, 4> replacements;
    for (size_t i = 0; i != ignore.size(); ++i)
        if (package) package->ResourcePlan(categories[i], replacements[i], ignore[i]);
    const auto addMod = [&](size_t i, std::uint8_t kind) {
        for (const auto& path : replacements[i]) {
            mounts.archives.push_back(resolve(path));
            mounts.archive_kinds.push_back(kind);
        }
    };
    const auto addBase = [&](size_t i, std::uint8_t kind) {
        if (ignore[i]) return;
        mounts.archives.push_back((resourceRoot / categories[i]).wstring());
        mounts.archive_kinds.push_back(kind);
    };
    addMod(0, owned::kArchiveFigures);
    addMod(3, owned::kArchiveFigures);
    addMod(1, owned::kArchiveTextures);
    addBase(0, owned::kArchiveFigures);
    addBase(1, owned::kArchiveTextures);
    addMod(2, owned::kArchiveTextures);
    addBase(2, owned::kArchiveTextures);
    addBase(3, owned::kArchiveAllKinds);
    addMod(3, owned::kArchiveTextures);
}

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
    CaptureGraphicsMounts(paths.mounts, resourceRoot, package, [&](const std::string& replacement) {
        return ResolveProfilePath(std::filesystem::path(replacement), profileDirectory).wstring();
    });
    const auto captureFamily = [&](const char* name, owned::ResourceStoreConfig& mounts) {
        std::vector<std::string> replacements;
        bool ignore = false;
        if (package) package->ResourcePlan(name, replacements, ignore);
        for (const auto& replacement : replacements)
            mounts.archives.push_back(ResolveProfilePath(
                std::filesystem::path(replacement), profileDirectory).wstring());
        if (!ignore) mounts.archives.push_back((resourceRoot / name).wstring());
    };
    captureFamily("texts.res", paths.text_mounts);
    captureFamily("database.res", paths.data_mounts);
    return paths;
}
}
