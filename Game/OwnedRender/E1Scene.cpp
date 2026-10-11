#include "E1Scene.h"

namespace {
std::uint64_t advance(std::uint64_t& value) noexcept {
    if (++value == 0) ++value;
    return value;
}
}
namespace OwnedRender {
bool E1SceneMembership::Matches(const void* source, std::uint64_t generation) const noexcept {
    return source && generation && owner == source && world == generation;
}
void E1SceneMembership::Begin(const void* source, std::uint64_t generation) noexcept {
    if (!source || !generation || Matches(source, generation)) return;
    scene.reset();
    members.clear();
    resources.clear();
    owner = source;
    world = generation;
    epoch = advance(nextEpoch);
    scene.positive_lifecycle_reset(epoch);
}
void E1SceneMembership::Retire(const void* source, std::uint64_t generation) noexcept {
    if (!Matches(source, generation)) return;
    scene.reset();
    members.clear();
    resources.clear();
    owner = nullptr;
    world = epoch = 0;
}
bool E1SceneMembership::Added(const void* source, std::uint64_t generation, std::int32_t id) noexcept {
    if (!Matches(source, generation)) return false;
    // c56's live membership domain is positive. Nonpositive client/UI objects
    // must not be relabelled into that domain or admitted as world models.
    if (id <= 0) return true;
    try {
        auto result = members.emplace(id, owned::LifecycleIdentity{});
        if (result.second) result.first->second = {id, advance(nextMember), epoch};
        scene.lifecycle_added(result.first->second);
        return true;
    } catch (...) {
        // Do not expose an earlier world membership after a failed publication.
        Retire(source, generation);
        return false;
    }
}
void E1SceneMembership::Removed(const void* source, std::uint64_t generation, std::int32_t id) noexcept {
    if (!Matches(source, generation) || id <= 0) return;
    scene.lifecycle_removed(id);
    members.erase(id);
}
void E1SceneMembership::ResetPositive(const void* source, std::uint64_t generation) noexcept {
    if (!Matches(source, generation)) return;
    members.clear();
    scene.positive_lifecycle_reset(epoch = advance(nextEpoch));
}
owned::VisualResourceIncarnation E1SceneMembership::BeginResource(const void* source,
    const void* object, std::int32_t id) noexcept {
    if (!source || source != owner || !world || !object || id <= 0) return {};
    DestroyResource(object);
    const auto resource = scene.visual_begin_resource(id);
    if (!resource.serial) return {};
    try { resources.emplace(object, resource); }
    catch (...) { scene.visual_finish_resource(resource, false); return {}; }
    return resource;
}
bool E1SceneMembership::FinishResource(owned::VisualResourceIncarnation resource, bool success) noexcept {
    if (!resource.serial) return false;
    const bool finished = scene.visual_finish_resource(resource, success);
    if (!success) {
        for (auto it = resources.begin(); it != resources.end(); ++it) {
            if (it->second.signed_id == resource.signed_id && it->second.serial == resource.serial) {
                resources.erase(it);
                break;
            }
        }
    }
    return finished;
}
void E1SceneMembership::DestroyResource(const void* object) noexcept {
    const auto it = resources.find(object);
    if (it == resources.end()) return;
    // Token-aware retirement cannot delete another object's reused ID.
    scene.visual_finish_resource(it->second, false);
    resources.erase(it);
}
owned::VisualResourceIncarnation E1SceneMembership::Resource(const void* object) const noexcept {
    const auto it = resources.find(object);
    if (it == resources.end()) return {};
    const auto current = scene.visual_resource(it->second.signed_id);
    return current.serial == it->second.serial ? current : owned::VisualResourceIncarnation{};
}
owned::LifecycleIdentity E1SceneMembership::Identity(std::int32_t id) const noexcept {
    const auto found = members.find(id);
    return found == members.end() ? owned::LifecycleIdentity{} : found->second;
}
}
