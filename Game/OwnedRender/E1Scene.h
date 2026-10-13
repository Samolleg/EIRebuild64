#pragma once
#include "retained_static_scene.h"
#include "owned_resource_runtime.h"
#include <map>
#include <memory>

namespace OwnedRender {
struct E1AnimationSelection {
    owned::VisualClipSelection visual{};
    std::uint64_t source_generation = 0;
    bool valid = false;
};
// Keeps the host-selected ADB and ANM byte selections stable for one
// animation-system resource. The implementation is private to E1Scene.cpp.
class E1AnimationSelectionCache {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    E1AnimationSelectionCache();
    ~E1AnimationSelectionCache();
    E1AnimationSelectionCache(const E1AnimationSelectionCache&) = delete;
    E1AnimationSelectionCache& operator=(const E1AnimationSelectionCache&) = delete;
    void Clear() noexcept;
    bool Select(std::uint64_t world_epoch, std::uint64_t resource_serial,
        const std::string& mask_name, const void* model_identity,
        const std::string& database_name, const void* selected_database,
        std::uint32_t database_size, const std::string& animation_name,
        const std::string& animation_resource_name, const void* selected_animation,
        std::uint32_t animation_size, std::uint32_t ordinal,
        const std::string& resolved_clip_name, E1AnimationSelection& output) noexcept;
};
// These spans are the exact ADB and ANM selections already made by the host
// resource managers. The renderer copies and decodes them; it never searches
// for a second resource or retains a host pointer.
bool E1BuildAnimationSelection(const std::string& database_name,
    const void* selected_database, std::uint32_t database_size,
    const std::string& animation_name, const std::string& animation_resource_name,
    const void* selected_animation, std::uint32_t animation_size,
    std::uint32_t ordinal, E1AnimationSelection& output,
    const std::string& resolved_clip_name = std::string()) noexcept;

// Host membership is published at successful list mutations, not visibility
// traversal. No engine object pointer enters the renderer's retained scene.
class E1SceneMembership {
    owned::RetainedStaticScene& scene;
    const void* owner = nullptr;
    std::uint64_t world = 0, epoch = 0, nextEpoch = 0, nextMember = 0;
    std::map<std::int32_t, owned::LifecycleIdentity> members;
    std::map<const void*, owned::VisualResourceIncarnation> resources;
    bool Matches(const void* source, std::uint64_t generation) const noexcept;
    void RetireResources() noexcept;
public:
    explicit E1SceneMembership(owned::RetainedStaticScene& target) noexcept : scene(target) {}
    E1SceneMembership(const E1SceneMembership&) = delete;
    E1SceneMembership& operator=(const E1SceneMembership&) = delete;
    void Begin(const void* source, std::uint64_t generation) noexcept;
    // The positive world epoch the retained scene was reset to: the renderer's
    // world epoch (game.exe bridge: lifecycle positive_world_epoch).
    std::uint64_t Epoch() const noexcept { return owner ? epoch : 0; }
    bool CurrentWorld(std::uint64_t generation) const noexcept {
        return owner && generation && world == generation && epoch;
    }
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
    bool BindDatabase(owned::VisualResourceIncarnation resource,
        const E1AnimationSelection& selection) noexcept;
    bool PublishObjectPose(owned::VisualResourceIncarnation resource,
        const owned::VisualObjectPose& pose) noexcept;
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
