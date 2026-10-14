#include "frame_statistics.h"

#include <algorithm>

namespace owned {

void FrameStatistics::add_present(std::int64_t timestamp, std::int64_t frequency) noexcept {
    if (timestamp <= 0 || frequency <= 0) {
        reset();
        return;
    }

    if (!has_anchor_) {
        start_window(timestamp, frequency);
        return;
    }

    // QueryPerformanceCounter uses one fixed frequency.  Treat a supplied
    // change as a new clock epoch rather than mixing incomparable tick units.
    if (frequency != frequency_) {
        start_window(timestamp, frequency);
        return;
    }

    if (timestamp <= last_timestamp_) {
        reset();
        return;
    }

    const std::int64_t interval_ticks = timestamp - last_timestamp_;
    // One second is a valid slow Present; a larger gap is a discontinuity.
    if (interval_ticks > frequency_) {
        start_window(timestamp, frequency);
        return;
    }

    if (count_ == kCapacity) pop_oldest();
    const std::size_t tail = (head_ + count_) % kCapacity;
    intervals_[tail] = {last_timestamp_, timestamp};
    ++count_;
    last_timestamp_ = timestamp;
    evict_expired();
}

void FrameStatistics::reset() noexcept {
    head_ = 0;
    count_ = 0;
    last_timestamp_ = 0;
    frequency_ = 0;
    has_anchor_ = false;
}

FrameStatisticsSnapshot FrameStatistics::snapshot() const noexcept {
    FrameStatisticsSnapshot result{};
    result.sample_count = count_;
    if (!count_) return result;

    const auto& oldest = interval_at(0);
    // Retained intervals are consecutive, so this span is their summed duration.
    const std::int64_t covered_ticks = last_timestamp_ - oldest.begin_timestamp;
    result.covered_seconds = static_cast<double>(covered_ticks) /
                             static_cast<double>(frequency_);
    result.average_fps = static_cast<double>(count_) / result.covered_seconds;

    std::int64_t longest_interval = 0;
    for (std::size_t index = 0; index < count_; ++index) {
        const auto& interval = interval_at(index);
        longest_interval = std::max(longest_interval,
                                    interval.end_timestamp - interval.begin_timestamp);
    }
    result.lowest_fps = static_cast<double>(frequency_) /
                        static_cast<double>(longest_interval);

    result.complete = at_least_window(covered_ticks, frequency_);
    result.capacity_truncated = count_ == kCapacity && !result.complete;
    result.warming_up = !result.complete && !result.capacity_truncated;
    return result;
}

void FrameStatistics::start_window(std::int64_t timestamp, std::int64_t frequency) noexcept {
    head_ = 0;
    count_ = 0;
    last_timestamp_ = timestamp;
    frequency_ = frequency;
    has_anchor_ = true;
}

void FrameStatistics::pop_oldest() noexcept {
    if (!count_) return;
    head_ = (head_ + 1) % kCapacity;
    --count_;
}

void FrameStatistics::evict_expired() noexcept {
    while (count_) {
        const auto& oldest = interval_at(0);
        const std::int64_t age_ticks = last_timestamp_ - oldest.end_timestamp;
        if (!at_least_window(age_ticks, frequency_)) return;
        pop_oldest();
    }
}

const FrameStatistics::Interval& FrameStatistics::interval_at(std::size_t offset) const noexcept {
    return intervals_[(head_ + offset) % kCapacity];
}

bool FrameStatistics::at_least_window(std::int64_t ticks, std::int64_t frequency) noexcept {
    return ticks / frequency >= kWindowSeconds;
}

} // namespace owned
