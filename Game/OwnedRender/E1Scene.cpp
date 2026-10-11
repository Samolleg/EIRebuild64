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
    owner = source;
    world = generation;
    epoch = advance(nextEpoch);
    scene.positive_lifecycle_reset(epoch);
}
void E1SceneMembership::Retire(const void* source, std::uint64_t generation) noexcept {
    if (!Matches(source, generation)) return;
    scene.reset();
    members.clear();
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
owned::LifecycleIdentity E1SceneMembership::Identity(std::int32_t id) const noexcept {
    const auto found = members.find(id);
    return found == members.end() ? owned::LifecycleIdentity{} : found->second;
}
}
