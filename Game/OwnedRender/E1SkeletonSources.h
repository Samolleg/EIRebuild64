#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned { struct ResourceBlob; struct ModelCpuAsset; }
namespace OwnedRender {
// Scoped to the actual loader owner, not a global filename cache.
class E1SelectedSkeletonBytes {
    std::shared_ptr<const owned::ResourceBlob> selected;
    bool known = false;
public:
    bool Publish(const char* name, const void* data, std::uint32_t size) noexcept;
    void Clear() noexcept { selected.reset(); known = false; }
    bool Known() const noexcept { return known; }
    const std::shared_ptr<const owned::ResourceBlob>& Get() const noexcept { return selected; }
};
struct E1SkeletonNode {
    const void* node = nullptr;
    const void* parent = nullptr;
    std::string name, parentName;
    E1SelectedSkeletonBytes bone;
};
class E1SkeletonSources {
    E1SelectedSkeletonBytes links;
    std::string visual;
    std::vector<E1SkeletonNode> previous;
    const void* previousHead = nullptr;
    std::shared_ptr<const owned::ModelCpuAsset> completed;
public:
    void Clear() noexcept;
    void PublishLinks(const char* name, const void* data, std::uint32_t size) noexcept;
    std::shared_ptr<const owned::ModelCpuAsset> Complete(
        const std::vector<E1SkeletonNode>& nodes, const void* head) noexcept;
};
}
