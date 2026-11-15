#pragma once
#include "../RuntimePaths64.h"
#include "ResourceBootstrap.h"
#include <filesystem>
#include <array>
#include <algorithm>
#include "../../external/owned-renderer/Source/runtime_diagnostics.h"

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
    const auto addMod = [&](size_t i, std::uint8_t kind, bool pak) {
        for (const auto& path : replacements[i]) {
            if (std::filesystem::is_directory(path)) {
                if (pak) mounts.loose_roots.push_back(resolve(path));
                continue;
            }
            if ((_stricmp(std::filesystem::path(path).extension().string().c_str(),".pak") == 0) != pak) continue;
            const auto archive = std::filesystem::path(resolve(path)).lexically_normal().wstring();
            const auto existing = std::find_if(mounts.archives.begin(), mounts.archives.end(),
                [&](const std::wstring& mounted) { return _wcsicmp(mounted.c_str(), archive.c_str()) == 0; });
            if (existing != mounts.archives.end()) {
                mounts.archive_kinds[existing - mounts.archives.begin()] |= kind;
            } else {
                mounts.archives.push_back(archive);
                mounts.archive_kinds.push_back(kind);
            }
        }
    };
    for (bool pak : {true, false}) {
        addMod(0, owned::kArchiveFigures, pak);
        addMod(1, owned::kArchiveTextures, pak);
        addMod(2, owned::kArchiveTextures, pak);
        addMod(3, owned::kArchiveAllKinds, pak);
    }
    for (size_t i = 0; i != ignore.size(); ++i) {
        if (ignore[i]) continue;
        mounts.archives.push_back((resourceRoot / categories[i]).wstring());
        mounts.archive_kinds.push_back(i == 0 ? owned::kArchiveFigures :
            i == 3 ? owned::kArchiveAllKinds : owned::kArchiveTextures);
    }
}

inline std::filesystem::path AbsoluteInput(const std::filesystem::path& value) {
    if (!value.is_absolute() || value.wstring().find(L'\0') != std::wstring::npos) throw std::invalid_argument("relative startup resource path");
    return value;
}

// Package is CModPackage in production. Copy its already selected resource
// plan, combining archive categories and preserving the ignore-original flag.
template<class Package>
owned::EmbeddedHostPaths CaptureStartupPaths(const std::filesystem::path& engine,
    const std::filesystem::path& work,
    const std::filesystem::path& resources, const Package* package) {
    const auto workRoot = AbsoluteInput(work);
    const auto resourceRoot = AbsoluteInput(resources);
    owned::EmbeddedHostPaths paths;
    if (!engine.is_absolute()) throw std::invalid_argument("relative engine directory");
    paths.renderer_config = RuntimePaths64::Config("rddraw.ini").wstring();
    const auto modRoot = package ? AbsoluteInput(std::filesystem::path(package->Directory())) : workRoot;
    paths.world_map_layouts = (modRoot / L"world-map-layouts.json").wstring();
    if (package) paths.mounts.loose_roots.push_back((modRoot / L"Maps").wstring());
    paths.mounts.loose_roots.push_back((workRoot / L"Maps").wstring());
    CaptureGraphicsMounts(paths.mounts, resourceRoot, package, [&](const std::string& replacement) {
        return AbsoluteInput(std::filesystem::path(replacement)).wstring();
    });
    const auto captureFamily = [&](const char* name, owned::ResourceStoreConfig& mounts) {
        std::vector<std::string> replacements;
        bool ignore = false;
        if (package) package->ResourcePlan(name, replacements, ignore);
        for (const auto& replacement : replacements) {
            const auto path = AbsoluteInput(std::filesystem::path(replacement));
            if (std::filesystem::is_directory(path)) {
                mounts.loose_roots.push_back(path.wstring());
            } else mounts.archives.push_back(path.wstring());
        }
        if (!ignore) mounts.archives.push_back((resourceRoot / name).wstring());
    };
    captureFamily("texts.res", paths.text_mounts);
    captureFamily("database.res", paths.data_mounts);
    return paths;
}
}
