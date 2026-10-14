#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace owned {

struct FrameStatisticsSnapshot {
    std::size_t sample_count = 0;
    double covered_seconds = 0.0;
    double average_fps = 0.0;
    double lowest_fps = 0.0;
    bool warming_up = true;
    bool complete = false;
    bool capacity_truncated = false;
};

// Stores only valid consecutive Present intervals.  The first valid timestamp
// establishes an anchor and intentionally creates no interval.  Intervals that
// end five seconds or more before the latest Present are evicted.  A clock
// discontinuity never creates an interval: invalid timestamps clear the state,
// while a long gap or changed positive frequency starts a new warm-up anchor.
class FrameStatistics {
public:
    static constexpr std::size_t kCapacity = 8192;
    static constexpr std::int64_t kWindowSeconds = 5;

    void add_present(std::int64_t timestamp, std::int64_t frequency) noexcept;
    void reset() noexcept;
    FrameStatisticsSnapshot snapshot() const noexcept;

private:
    struct Interval {
        std::int64_t begin_timestamp = 0;
        std::int64_t end_timestamp = 0;
    };

    void start_window(std::int64_t timestamp, std::int64_t frequency) noexcept;
    void pop_oldest() noexcept;
    void evict_expired() noexcept;
    const Interval& interval_at(std::size_t offset) const noexcept;
    static bool at_least_window(std::int64_t ticks, std::int64_t frequency) noexcept;

    std::array<Interval, kCapacity> intervals_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::int64_t last_timestamp_ = 0;
    std::int64_t frequency_ = 0;
    bool has_anchor_ = false;
};

} // namespace owned
