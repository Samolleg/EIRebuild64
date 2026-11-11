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
    std::string mountedArchives;
    if (RuntimeDiagnostics::Enabled()) {
        const auto resourceRoot = std::filesystem::path(
            (LPCSTR)theSettings.szResourcePath).lexically_normal();
        mountedArchives = " mounted_archives=[";
        for (size_t i = 0; i != archives; ++i) {
            const auto archive = std::filesystem::path(paths.mounts.archives[i]);
            const bool base = _wcsicmp(archive.lexically_normal().c_str(),
                (resourceRoot / archive.filename()).lexically_normal().c_str()) == 0;
            const char* source = base ? "base-res" :
                _wcsicmp(archive.extension().c_str(), L".pak") == 0 ? "mod-pak" : "mod-res";
            if (i) mountedArchives += ",";
            mountedArchives += source;
            mountedArchives += "=";
            mountedArchives += GameJournal::Quote(archive.string().c_str());
        }
        mountedArchives += "]";
    }
    owned::embedded_host_configuration().configure(std::move(paths));
    GameJournal::Event("RENDER", "resource_bootstrap",
        "source=GeneralSettings/ModRuntime roots=%zu archives=%zu device_created=0%s",
        roots, archives, mountedArchives.c_str());
}

ResourceRuntimeLease::ResourceRuntimeLease() {
    owned::owned_resource_runtime_acquire();
}

ResourceRuntimeLease::~ResourceRuntimeLease() {
    owned::owned_resource_runtime_release();
}
}
