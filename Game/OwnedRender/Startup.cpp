#include "StdAfx.h"
#include "Startup.h"
#include "StartupPaths.h"
#include "../SettingsProfile.h"
#include "../GeneralSettings.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"
#include "../GameJournal.h"

namespace OwnedRender {
void ConfigureStartup() {
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, _countof(module));
    if (!length || length >= _countof(module))
        throw std::runtime_error("Cannot determine EIRebuild engine directory");
    const auto engine = std::filesystem::path(module).parent_path();
    auto paths = CaptureStartupPaths(engine, std::filesystem::path(SettingsProfile::Path()),
        std::filesystem::path((LPCSTR)theSettings.szWorkPath),
        std::filesystem::path((LPCSTR)theSettings.szResourcePath), ModRuntime::Resources());
    const auto roots = paths.mounts.loose_roots.size();
    const auto archives = paths.mounts.archives.size();
    ConfigureResources(engine.wstring(), paths.world_map_layouts, std::move(paths.mounts));
    GameJournal::Event("RENDER", "resource_bootstrap",
        "source=SettingsProfile/ModRuntime roots=%zu archives=%zu device_created=0", roots, archives);
}
}
