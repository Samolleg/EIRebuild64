#include "StdAfx.h"
#include "Startup.h"
#include "StartupPaths.h"
#include "../GeneralSettings.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"
#include "../GameJournal.h"
#include <owned_resource_runtime.h>

namespace OwnedRender {
void ConfigureStartup() {
    auto paths = CaptureStartupPaths(RuntimePaths64::Engine(),
        std::filesystem::path((LPCSTR)theSettings.szWorkPath),
        std::filesystem::path((LPCSTR)theSettings.szResourcePath), ModRuntime::Resources());
    const auto roots = paths.mounts.loose_roots.size();
    const auto archives = paths.mounts.archives.size();
    owned::embedded_host_configuration().configure(std::move(paths));
    GameJournal::Event("RENDER", "resource_bootstrap",
        "source=GeneralSettings/ModRuntime roots=%zu archives=%zu device_created=0", roots, archives);
}

ResourceRuntimeLease::ResourceRuntimeLease() {
    owned::owned_resource_runtime_acquire();
}

ResourceRuntimeLease::~ResourceRuntimeLease() {
    owned::owned_resource_runtime_release();
}
}
