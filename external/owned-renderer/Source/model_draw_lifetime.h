#pragma once
#include <utility>

namespace owned {
// A synchronous draw owns one extra native reference. Acquire it before a
// cache miss trims the cache, then release it after all figure uses on every
// exit. Neither native object pointers nor release callbacks enter a queue.
class ModelDrawReference {
    void* value_ = nullptr;
    void (*release_)(void*) noexcept = nullptr;
public:
    ModelDrawReference() noexcept = default;
    ModelDrawReference(const ModelDrawReference&) = delete;
    ModelDrawReference& operator=(const ModelDrawReference&) = delete;
    ~ModelDrawReference() { reset(); }
    void reset() noexcept {
        void* value = std::exchange(value_, nullptr);
        auto release = std::exchange(release_, nullptr);
        if (value) release(value);
    }
    template<class Retain>
    void hold(void* value, Retain&& retain, void (*release)(void*) noexcept) noexcept {
        if (value == value_) return;
        reset();
        if (!value) return;
        retain(value);
        value_ = value;
        release_ = release;
    }
};

// Both native and typed manager adapters retire renderer resources before
// the manager starts destroying its surfaces. Unrelated managers do nothing.
template<class Match, class Retire, class Shutdown>
bool retire_model_texture_manager(void* owner, Match&& match,
    Retire&& retire, Shutdown&& shutdown) noexcept {
    if (!owner || !match(owner)) return false;
    retire();
    shutdown();
    return true;
}
} // namespace owned
