#pragma once
#include <atomic>
#include <cstdint>

namespace OwnedRender {
// No game pointers are retained. Only the expiry of an observed replacement.
class E1SaveClashNotice {
public:
    bool Observe(const void* previous, const void* incoming, std::uint64_t now) noexcept {
        if (!previous || !incoming || previous == incoming) return false;
        started.store(now, std::memory_order_relaxed);
        active.store(true, std::memory_order_release);
        return true;
    }
    bool Current(std::uint64_t now) const noexcept {
        if (!active.load(std::memory_order_acquire)) return false;
        const auto begin = started.load(std::memory_order_relaxed);
        return now >= begin && now - begin < 20000;
    }
    void Clear() noexcept { active.store(false, std::memory_order_release); }
private:
    std::atomic<std::uint64_t> started{0};
    std::atomic<bool> active{false};
};
void E1ObserveSaveReplacement(int id, const void* previous, const void* incoming) noexcept;
void E1ClearSaveNotice() noexcept;
}
