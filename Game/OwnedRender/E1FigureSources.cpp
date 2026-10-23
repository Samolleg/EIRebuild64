#include "E1FigureSources.h"
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include <cstring>

namespace OwnedRender {
bool E1FigureSources::Publish(const std::string& part, const void* selected,
    std::uint32_t size) noexcept try {
    if (part.empty() || !selected || !size) { Remove(part); return false; }
    const auto found = entries.find(part);
    if (found != entries.end() && found->second.bytes->size() == size &&
        std::memcmp(found->second.bytes->data(), selected, size) == 0) return true;
    Remove(part); // failed replacement cannot leave an earlier selected FIG
    // ResourceStore issues the same globally unique immutable identities as
    // the other renderer inputs, including another container with this name.
    static const owned::ResourceStore store;
    owned::ResourceBlob blob;
    owned::ResourceFailure failure;
    if (!store.copy_selected_memory(part, selected, size, blob, failure)) return false;
    auto figure = owned::retained_visual_figure_asset(blob, failure);
    if (!figure) return false;
    entries.emplace(part, Entry{std::move(blob.bytes), std::move(figure)});
    return true;
} catch (...) { Remove(part); return false; }
bool E1FigureSources::ConfirmPositionSource(const std::string& part,
    const void* selected, std::uint32_t size) noexcept {
    const auto found = entries.find(part);
    if (found != entries.end() && selected && found->second.bytes->size() == size &&
        std::memcmp(found->second.bytes->data(), selected, size) == 0) return true;
    Remove(part);
    return false;
}
void E1FigureSources::Remove(const std::string& part) noexcept { entries.erase(part); }
std::shared_ptr<const owned::FigureCpuAsset> E1FigureSources::Find(const std::string& part) const noexcept {
    const auto found = entries.find(part);
    return found == entries.end() ? nullptr : found->second.figure;
}
}
