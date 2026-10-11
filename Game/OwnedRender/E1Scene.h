#pragma once
#include "retained_static_scene.h"
#include "owned_resource_runtime.h"
#include <map>

namespace OwnedRender {
// Host membership is published at successful list mutations, not visibility
// traversal. No engine object pointer enters the renderer's retained scene.
class E1SceneMembership {
    owned::RetainedStaticScene& scene;
    const void* owner = nullptr;
    std::uint64_t world = 0, epoch = 0, nextEpoch = 0, nextMember = 0;
    std::map<std::int32_t, owned::LifecycleIdentity> members;
    std::map<const void*, owned::VisualResourceIncarnation> resources;
    bool Matches(const void* source, std::uint64_t generation) const noexcept;
public:
    explicit E1SceneMembership(owned::RetainedStaticScene& target) noexcept : scene(target) {}
    E1SceneMembership(const E1SceneMembership&) = delete;
    E1SceneMembership& operator=(const E1SceneMembership&) = delete;
    void Begin(const void* source, std::uint64_t generation) noexcept;
    void Retire(const void* source, std::uint64_t generation) noexcept;
    bool Added(const void* source, std::uint64_t generation, std::int32_t id) noexcept;
    void Removed(const void* source, std::uint64_t generation, std::int32_t id) noexcept;
    void ResetPositive(const void* source, std::uint64_t generation) noexcept;
    owned::LifecycleIdentity Identity(std::int32_t id) const noexcept;
    // Resource creation is independent of world membership and visibility.
    // Host pointers only identify the synchronous owner; they never enter c56.
    owned::VisualResourceIncarnation BeginResource(const void* source,
        const void* object, std::int32_t id) noexcept;
    bool FinishResource(owned::VisualResourceIncarnation resource, bool success) noexcept;
    void DestroyResource(const void* object) noexcept;
    owned::VisualResourceIncarnation Resource(const void* object) const noexcept;
    owned::RetainedStaticScene& Scene() noexcept { return scene; }
};
class E1ResourceCreation {
    E1SceneMembership& host;
    owned::VisualResourceIncarnation resource;
public:
    E1ResourceCreation(E1SceneMembership& target, const void* world,
        const void* object, std::int32_t id) noexcept
        : host(target), resource(host.BeginResource(world, object, id)) {}
    ~E1ResourceCreation() noexcept { host.FinishResource(resource, false); }
    void Complete(bool success) noexcept {
        host.FinishResource(resource, success);
        resource = {};
    }
    E1ResourceCreation(const E1ResourceCreation&) = delete;
    E1ResourceCreation& operator=(const E1ResourceCreation&) = delete;
};
inline E1SceneMembership& E1Scene() noexcept {
    static E1SceneMembership value(owned::retained_visual_scene());
    return value;
}
}
