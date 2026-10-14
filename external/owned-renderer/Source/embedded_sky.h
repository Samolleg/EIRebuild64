#pragma once
#include <cstdint>
// Render-thread device ownership, independent of sky quality/world lifetime.
// The owner is an opaque identity and is never dereferenced here.
namespace owned {
class EmbeddedSky {
    const void* owner_ = nullptr;
    bool enabled_ = false;
    std::uint64_t revision_ = 0;
    void changed() noexcept { if (++revision_ == 0) ++revision_; }
public:
    bool available_to(const void* owner) const noexcept { return !owner_ || owner_ == owner; }
    bool attach(const void* owner) noexcept {
        if (!owner || !available_to(owner)) return false;
        if (owner_ != owner) { owner_ = owner; changed(); } return true;
    }
    void detach(const void* owner) noexcept {
        if (owner_ && owner_ == owner) { owner_ = nullptr; enabled_ = false; changed(); }
    }
    void configure(bool enabled) noexcept { if (enabled_ != enabled) { enabled_ = enabled; changed(); } }
    std::uint64_t revision() const noexcept { return revision_; }
    bool installed() const noexcept { return owner_ != nullptr; }
    bool enabled() const noexcept { return installed() && enabled_; }
};
inline EmbeddedSky& embedded_sky() noexcept { static EmbeddedSky sky; return sky; }
}
