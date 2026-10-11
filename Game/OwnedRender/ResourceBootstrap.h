#pragma once
#include "../../external/owned-renderer/Source/embedded_host.h"

namespace OwnedRender {
// Values are resolved by SettingsProfile and ModRuntime, not by the renderer.
// Keep archive order, including duplicates: first matching resource wins.
inline void ConfigureResources(const std::wstring& engineDirectory,
    const std::wstring& worldMapLayouts, owned::ResourceStoreConfig mounts) {
    if (engineDirectory.empty())
        throw std::invalid_argument("missing EIRebuild engine directory");
    owned::EmbeddedHostPaths paths;
    paths.mounts = std::move(mounts);
    paths.renderer_config = engineDirectory;
    if (paths.renderer_config.back() != L'\\' && paths.renderer_config.back() != L'/')
        paths.renderer_config += L'\\';
    paths.renderer_config += L"rddraw.ini";
    paths.world_map_layouts = worldMapLayouts;
    owned::embedded_host_configuration().configure(std::move(paths));
}
}
