#include "E1Scene.h"
#include "owned_resource_runtime.h"
#include "owned_resource_store.h"

namespace {
std::uint64_t advance(std::uint64_t& value) noexcept {
    if (++value == 0) ++value;
    return value;
}
}
namespace OwnedRender {
struct E1AnimationSelectionCache::Impl {
    std::uint64_t worldEpoch = 0;
    std::uint64_t resourceSerial = 0;
    const void* modelIdentity = nullptr;
    std::string maskName;
    std::string databaseName;
    const void* selectedDatabase = nullptr;
    std::uint32_t databaseSize = 0;
    std::shared_ptr<const owned::AnimationDatabaseAsset> database;
    std::uint64_t databaseGeneration = 0;
    std::string animationResourceName;
    const void* selectedAnimation = nullptr;
    std::uint32_t animationSize = 0;
    owned::ResourceBlob animationBlob;
    std::uint64_t animationGeneration = 0;
    std::string animationName;
    std::string resolvedClipName;
    E1AnimationSelection selection;
};

E1AnimationSelectionCache::E1AnimationSelectionCache() = default;
E1AnimationSelectionCache::~E1AnimationSelectionCache() = default;
void E1AnimationSelectionCache::Clear() noexcept { impl.reset(); }

bool E1AnimationSelectionCache::Select(std::uint64_t worldEpoch,
    std::uint64_t resourceSerial, const std::string& maskName,
    const void* modelIdentity, const std::string& databaseName,
    const void* selectedDatabase, std::uint32_t databaseSize,
    const std::string& animationName, const std::string& animationResourceName,
    const void* selectedAnimation, std::uint32_t animationSize,
    std::uint32_t ordinal, const std::string& resolvedClipName,
    E1AnimationSelection& output) noexcept try {
    output = {};
    if (maskName.empty() || !modelIdentity || databaseName.empty() ||
        !selectedDatabase || !databaseSize || animationName.empty() ||
        animationResourceName.empty() || !selectedAnimation || !animationSize)
        return false;

    if (!impl) impl.reset(new Impl());
    Impl& cache = *impl;
    if (cache.worldEpoch != worldEpoch || cache.resourceSerial != resourceSerial ||
        cache.modelIdentity != modelIdentity || cache.maskName != maskName) {
        cache = Impl{};
        cache.worldEpoch = worldEpoch;
        cache.resourceSerial = resourceSerial;
        cache.modelIdentity = modelIdentity;
        cache.maskName = maskName;
    }

    if (cache.databaseName != databaseName ||
        cache.selectedDatabase != selectedDatabase || cache.databaseSize != databaseSize) {
        cache.database.reset();
        cache.databaseGeneration = 0;
        cache.databaseName = databaseName;
        cache.selectedDatabase = selectedDatabase;
        cache.databaseSize = databaseSize;
        cache.selection = {};
    }
    if (!cache.database) {
        owned::ResourceFailure failure;
        cache.database = owned::retained_visual_selected_database_asset(databaseName,
            selectedDatabase, databaseSize, cache.databaseGeneration, failure);
        if (!cache.database || !cache.databaseGeneration) return false;
    }

    if (cache.animationResourceName != animationResourceName ||
        cache.selectedAnimation != selectedAnimation || cache.animationSize != animationSize) {
        cache.animationBlob = {};
        cache.animationGeneration = 0;
        cache.animationResourceName = animationResourceName;
        cache.selectedAnimation = selectedAnimation;
        cache.animationSize = animationSize;
        cache.animationName.clear();
        cache.resolvedClipName.clear();
        cache.selection = {};
    }
    if (!cache.animationBlob.bytes) {
        owned::ResourceFailure failure;
        if (!owned::retained_visual_copy_selected_memory(animationResourceName,
            selectedAnimation, animationSize, cache.animationBlob,
            cache.animationGeneration, failure) || !cache.animationGeneration ||
            cache.animationGeneration != cache.databaseGeneration)
            return false;
    }

    if (!cache.selection.valid || cache.animationName != animationName ||
        cache.resolvedClipName != resolvedClipName) {
        owned::ResourceFailure failure;
        auto clip = owned::retained_visual_clip_asset(cache.animationBlob,
            resolvedClipName.empty() ? animationName : resolvedClipName,
            failure, cache.database, cache.databaseGeneration);
        if (!clip) return false;
        cache.selection = {};
        cache.selection.visual.database = cache.database;
        cache.selection.visual.clip = std::move(clip);
        cache.selection.visual.neutral = false;
        cache.selection.source_generation = cache.databaseGeneration;
        cache.selection.valid = true;
        cache.animationName = animationName;
        cache.resolvedClipName = resolvedClipName;
    }
    cache.selection.visual.ordinal = ordinal;
    output = cache.selection;
    return true;
} catch (...) { output = {}; return false; }

bool E1BuildAnimationSelection(const std::string& databaseName,
    const void* selectedDatabase, std::uint32_t databaseSize,
    const std::string& animationName, const std::string& animationResourceName,
    const void* selectedAnimation, std::uint32_t animationSize,
    std::uint32_t ordinal, E1AnimationSelection& output,
    const std::string& resolvedClipName) noexcept try {
    output = {};
    if (databaseName.empty() || animationName.empty() || animationResourceName.empty() ||
        !selectedDatabase || !databaseSize || !selectedAnimation || !animationSize) return false;

    owned::ResourceFailure failure;
    std::uint64_t generation = 0;
    auto database = owned::retained_visual_selected_database_asset(databaseName,
        selectedDatabase, databaseSize, generation, failure);
    if (!database || !generation || ordinal >= database->entries.size() ||
        database->entries[ordinal].name != animationName) return false;

    owned::ResourceBlob animation;
    std::uint64_t animationGeneration = 0;
    if (!owned::retained_visual_copy_selected_memory(animationResourceName,
        selectedAnimation, animationSize, animation, animationGeneration, failure) ||
        !animationGeneration) return false;
    auto clip = owned::retained_visual_clip_asset(animation,
        resolvedClipName.empty() ? animationName : resolvedClipName,
        failure, database, generation);
    if (!clip) return false;

    output.visual.database = std::move(database);
    output.visual.clip = std::move(clip);
    output.visual.ordinal = ordinal;
    output.visual.neutral = false;
    output.source_generation = generation;
    output.valid = true;
    return true;
} catch (...) { output = {}; return false; }

bool E1SceneMembership::Matches(const void* source, std::uint64_t generation) const noexcept {
    return source && generation && owner == source && world == generation;
}
void E1SceneMembership::RetireResources() noexcept {
    for (const auto& entry : resources) scene.visual_finish_resource(entry.second, false);
    resources.clear();
}
void E1SceneMembership::Begin(const void* source, std::uint64_t generation) noexcept {
    if (!source || !generation || Matches(source, generation)) return;
    // A new client world keeps the server-published MOB source: the main
    // zone load publishes it before the client world begins, and a full
    // reset here erased it (no static scene, shadows or point lamps).
    RetireResources();
    members.clear();
    owner = source;
    world = generation;
    epoch = advance(nextEpoch);
    scene.positive_lifecycle_reset(epoch);
}
void E1SceneMembership::Retire(const void* source, std::uint64_t generation) noexcept {
    if (!Matches(source, generation)) return;
    RetireResources();
    members.clear();
    scene.positive_lifecycle_reset(advance(nextEpoch));
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
bool E1SceneMembership::BindDatabase(owned::VisualResourceIncarnation resource,
    const E1AnimationSelection& selection) noexcept {
    return resource.serial && selection.valid && !selection.visual.neutral &&
        selection.visual.database && selection.source_generation &&
        scene.visual_bind_database(resource, selection.source_generation, selection.visual.database);
}
bool E1SceneMembership::PublishObjectPose(owned::VisualResourceIncarnation resource,
    const owned::VisualObjectPose& pose) noexcept {
    return resource.serial && scene.visual_publish_object_pose(resource, pose);
}
owned::LifecycleIdentity E1SceneMembership::Identity(std::int32_t id) const noexcept {
    const auto found = members.find(id);
    return found == members.end() ? owned::LifecycleIdentity{} : found->second;
}
}
