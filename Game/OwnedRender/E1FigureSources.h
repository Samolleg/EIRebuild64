#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace owned { struct FigureCpuAsset; }
namespace OwnedRender {
// One owner per actual geometry container. A figure is the immutable selected
// FIG, never the host's morphed vertex buffers or a second lookup by filename.
class E1FigureSources {
    struct Entry {
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        std::shared_ptr<const owned::FigureCpuAsset> figure;
    };
    std::map<std::string, Entry> entries;
public:
    bool Publish(const std::string& part, const void* selected, std::uint32_t size) noexcept;
    // A native complection edit changes positions only. A different complete
    // FIG cannot represent that mixed result; withdraw rather than invent it.
    bool ConfirmPositionSource(const std::string& part, const void* selected,
        std::uint32_t size) noexcept;
    void Remove(const std::string& part) noexcept;
    std::shared_ptr<const owned::FigureCpuAsset> Find(const std::string& part) const noexcept;
};
}
