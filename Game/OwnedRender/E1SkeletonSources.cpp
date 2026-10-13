#include "E1SkeletonSources.h"
#include "owned_asset_loader.h"
#include <cstring>

namespace OwnedRender {
bool E1SelectedSkeletonBytes::Publish(const char* name, const void* data,
    std::uint32_t size) noexcept try {
    if (!data) { selected.reset(); known = size == 0; return known; }
    if (selected && selected->bytes && selected->bytes->size() == size &&
        std::memcmp(selected->bytes->data(), data, size) == 0) return true;
    Clear();
    static const owned::ResourceStore store;
    auto blob = std::make_shared<owned::ResourceBlob>();
    owned::ResourceFailure failure;
    if (!name || !size || !store.copy_selected_memory(name, data, size, *blob, failure)) return false;
    selected = std::move(blob);
    known = true;
    return true;
} catch (...) { Clear(); return false; }

void E1SkeletonSources::Clear() noexcept {
    links.Clear(); visual.clear(); previous.clear(); previousHead = nullptr; completed.reset();
}
void E1SkeletonSources::PublishLinks(const char* name, const void* data,
    std::uint32_t size) noexcept try {
    Clear();
    if (name && links.Publish(name, data, size)) visual = name;
} catch (...) { Clear(); }

std::shared_ptr<const owned::ModelCpuAsset> E1SkeletonSources::Complete(
    const std::vector<E1SkeletonNode>& nodes, const void* head) noexcept try {
    bool same = completed && head == previousHead && nodes.size() == previous.size();
    for (std::size_t i = 0; same && i < nodes.size(); ++i) {
        const auto& a = nodes[i]; const auto& b = previous[i];
        same = a.node == b.node && a.parent == b.parent && a.name == b.name &&
            a.parentName == b.parentName && a.bone.Known() == b.bone.Known() && a.bone.Get() == b.bone.Get();
    }
    if (same) return completed;
    completed.reset(); previous.clear(); previousHead = nullptr;
    if (!links.Get() || !head || nodes.empty()) return nullptr;
    auto result = std::make_shared<owned::ModelCpuAsset>();
    owned::ResourceFailure failure;
    if (!owned::decode_selected_skeleton(*links.Get(), visual,
        [&](const std::string& name, owned::ResourceBlob& blob, owned::ResourceFailure& error) {
            for (const auto& node : nodes) if (node.name == name) {
                if (!node.bone.Known()) { error.code = owned::ResourceFailureCode::parse; return false; }
                if (!node.bone.Get()) { error.code = owned::ResourceFailureCode::not_found; return false; }
                blob = *node.bone.Get();
                // CAnimationNode consumes six or eight endpoints, never one.
                if (blob.bytes->size() != 72 && blob.bytes->size() != 96) {
                    error.code = owned::ResourceFailureCode::parse; return false;
                }
                return true;
            }
            error.code = owned::ResourceFailureCode::parse; return false;
        }, *result, failure) || result->links.size() != nodes.size()) return nullptr;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& link = result->links[i]; const auto& actual = nodes[i];
        const void* parent = link.parent_index == UINT_MAX ? nullptr : nodes[link.parent_index].node;
        if (actual.name != link.child || actual.parentName != link.parent || !actual.node ||
            !owned::shipped_node_link_matches(link, i, actual.node, head, actual.parent, parent)) return nullptr;
    }
    // Pose/bounds caches key both source identities, not shared_ptr addresses.
    // This is an ordered composite of immutable selections, not a file claim.
    result->bone_container.container_incarnation = links.Get()->identity.container_incarnation;
    result->bone_container.canonical_name = "selected-bones:";
    for (const auto& node : nodes) {
        const auto& bone = node.bone.Get();
        const auto key = bone ? bone->identity.key() : std::string("missing");
        result->bone_container.canonical_name += std::to_string(key.size()) + ":" + key;
        if (bone) result->bone_container.bytes += bone->identity.bytes;
    }
    previous = nodes; previousHead = head; completed = std::move(result);
    return completed;
} catch (...) { completed.reset(); previous.clear(); previousHead = nullptr; return nullptr; }
}
