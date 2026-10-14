#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace owned {

enum class LifecycleValidation : std::uint32_t {
    disabled = 0,
    active,
    active_without_draw_oracle,
    signature_mismatch,
    allocation_failure,
    patch_failure,
    contract_fault,
};

struct LifecycleIdentity {
    std::int32_t id = 0;
    std::uint64_t generation = 0;
    std::uint64_t positive_world_epoch = 0;
};

struct LifecycleProbeSnapshot {
    LifecycleValidation validation = LifecycleValidation::disabled;
    bool requested = false;
    bool observer_active = false;
    bool hooks_installed = false;
    bool draw_oracle_validated = false;
    bool fail_closed = false;
    bool coverage_ambiguous = false;
    bool overflowed = false;
    bool positive_coverage_armed = false;
    bool negative_membership_observed_only = true;
    bool zero_membership_observed_only = true;

    std::uint64_t positive_world_epoch = 0;
    std::uint64_t last_generation = 0;
    std::uint64_t active_positive = 0;
    std::uint64_t active_negative = 0;
    std::uint64_t active_zero = 0;

    std::uint64_t add_events = 0;
    std::uint64_t duplicate_add_events = 0;
    std::uint64_t duplicate_add_untracked = 0;
    std::uint64_t remove_calls = 0;
    std::uint64_t remove_events = 0;
    std::uint64_t remove_misses = 0;
    std::uint64_t remove_untracked_successes = 0;
    std::uint64_t remove_lookup_mismatches = 0;
    std::uint64_t positive_bulk_resets = 0;
    std::uint64_t tick_completions = 0;
    std::uint64_t lookup_misses = 0;
    std::uint64_t pre_reset_positive_draws = 0;
    std::uint64_t pre_reset_positive_draw_misses = 0;
    std::uint64_t post_reset_positive_draws = 0;
    std::uint64_t drawn_positive_id_misses = 0;
    std::uint64_t negative_draws_observed = 0;
    std::uint64_t negative_draw_misses = 0;
    std::uint64_t zero_draws_observed = 0;
    std::uint64_t zero_draw_misses = 0;
    std::uint64_t id_reuses = 0;
    std::uint64_t tick_nested_mutations = 0;
    std::uint64_t mutation_reentrant_events = 0;
    std::uint64_t hook_validation_failures = 0;
    std::uint64_t overflows = 0;
};

// Fixed-capacity, allocation-free observer state. Engine pointers are never
// accepted or retained: signed object ID plus renderer generation is the only
// mutable identity represented here.
class LifecycleObserver {
public:
    static constexpr std::size_t kCapacity = 32768;
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "Lifecycle observer capacity must be a power of two");

    LifecycleObserver() noexcept;

    void reset() noexcept;
    void set_runtime_status(bool requested, bool observer_active,
                            bool hooks_installed, bool draw_oracle_validated,
                            LifecycleValidation validation) noexcept;
    void fail_closed(LifecycleValidation validation) noexcept;

    void record_inserted(std::int32_t id) noexcept;
    void record_duplicate(std::int32_t id) noexcept;
    void record_remove_observation(std::int32_t id, bool present_before,
                                   bool present_after) noexcept;
    void record_positive_bulk_reset() noexcept;
    void record_tick_completion() noexcept;
    void record_draw_lookup(std::int32_t id) noexcept;
    void record_lookup_mismatch() noexcept;
    void record_tick_nested_mutation() noexcept;
    void record_mutation_reentrant_event() noexcept;

    // Diagnostics/testing only. This observer is not a renderer scene database
    // and must never become a source of render membership.
    bool active_identity(std::int32_t id, LifecycleIdentity& identity) const noexcept;
    LifecycleProbeSnapshot snapshot() const noexcept;

private:
    struct Entry {
        std::int32_t id = 0;
        std::uint64_t generation = 0;
        std::uint64_t positive_world_epoch = 0;
        bool occupied = false;
        bool active = false;
    };

    static std::size_t bucket(std::int32_t id) noexcept;
    std::size_t find_entry(std::int32_t id) const noexcept;
    std::size_t find_or_empty(std::int32_t id) const noexcept;
    void decrement_active(std::int32_t id) noexcept;
    void mark_overflow() noexcept;

    mutable SRWLOCK lock_{};
    std::array<Entry, kCapacity> entries_{};
    LifecycleProbeSnapshot counters_{};
};

LifecycleObserver& lifecycle_observer() noexcept;
const char* lifecycle_validation_name(LifecycleValidation validation) noexcept;
bool format_lifecycle_probe_json(char* output, std::size_t capacity,
                                 const LifecycleProbeSnapshot& snapshot) noexcept;

} // namespace owned
