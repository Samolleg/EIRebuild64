#pragma once

namespace OwnedRender {
// Called on the startup thread after settings and ModRuntime selection, before
// any renderer, scene or loader worker is created.
void ConfigureStartup();

// Owns the embedded renderer resource runtime for the application lifetime.
// Construct only after ConfigureStartup has published absolute mount paths.
class ResourceRuntimeLease {
public:
    ResourceRuntimeLease();
    ~ResourceRuntimeLease();
    ResourceRuntimeLease(const ResourceRuntimeLease&) = delete;
    ResourceRuntimeLease& operator=(const ResourceRuntimeLease&) = delete;
};
}
