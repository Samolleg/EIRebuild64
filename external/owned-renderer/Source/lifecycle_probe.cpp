#include "lifecycle_probe.h"

#include <cstdio>
#include <limits>

namespace owned {
namespace {

constexpr std::size_t kNoEntry = LifecycleObserver::kCapacity;

} // namespace

LifecycleObserver::LifecycleObserver() noexcept {
    InitializeSRWLock(&lock_);
}

std::size_t LifecycleObserver::bucket(std::int32_t id) noexcept {
    const auto bits = static_cast<std::uint32_t>(id);
    return static_cast<std::size_t>((bits * 2654435761u) &
                                    static_cast<std::uint32_t>(kCapacity - 1));
}

std::size_t LifecycleObserver::find_entry(std::int32_t id) const noexcept {
    std::size_t index = bucket(id);
    for (std::size_t scanned = 0; scanned < kCapacity; ++scanned) {
        const Entry& entry = entries_[index];
        if (!entry.occupied) return kNoEntry;
        if (entry.id == id) return index;
        index = (index + 1) & (kCapacity - 1);
    }
    return kNoEntry;
}

std::size_t LifecycleObserver::find_or_empty(std::int32_t id) const noexcept {
    std::size_t index = bucket(id);
    for (std::size_t scanned = 0; scanned < kCapacity; ++scanned) {
        const Entry& entry = entries_[index];
        if (!entry.occupied || entry.id == id) return index;
        index = (index + 1) & (kCapacity - 1);
    }
    return kNoEntry;
}

void LifecycleObserver::reset() noexcept {
    AcquireSRWLockExclusive(&lock_);
    for (Entry& entry : entries_) entry = {};
    counters_ = {};
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::set_runtime_status(
    bool requested, bool observer_active, bool hooks_installed,
    bool draw_oracle_validated, LifecycleValidation validation) noexcept {
    AcquireSRWLockExclusive(&lock_);
    counters_.requested = requested;
    counters_.observer_active = observer_active;
    counters_.hooks_installed = hooks_installed;
    counters_.draw_oracle_validated = draw_oracle_validated;
    counters_.validation = validation;
    counters_.fail_closed = requested && !observer_active;
    if (counters_.fail_closed) ++counters_.hook_validation_failures;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::fail_closed(LifecycleValidation validation) noexcept {
    AcquireSRWLockExclusive(&lock_);
    counters_.observer_active = false;
    counters_.fail_closed = true;
    counters_.coverage_ambiguous = true;
    counters_.validation = validation;
    ++counters_.hook_validation_failures;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::mark_overflow() noexcept {
    counters_.overflowed = true;
    counters_.coverage_ambiguous = true;
    ++counters_.overflows;
}

void LifecycleObserver::record_inserted(std::int32_t id) noexcept {
    AcquireSRWLockExclusive(&lock_);
    const std::size_t index = find_or_empty(id);
    if (index == kNoEntry ||
        counters_.last_generation == std::numeric_limits<std::uint64_t>::max()) {
        mark_overflow();
        ReleaseSRWLockExclusive(&lock_);
        return;
    }

    Entry& entry = entries_[index];
    if (entry.occupied && entry.active) {
        ++counters_.duplicate_add_events;
        ++counters_.duplicate_add_untracked;
        counters_.coverage_ambiguous = true;
        ReleaseSRWLockExclusive(&lock_);
        return;
    }
    if (entry.occupied) ++counters_.id_reuses;

    entry.occupied = true;
    entry.active = true;
    entry.id = id;
    entry.generation = ++counters_.last_generation;
    entry.positive_world_epoch = id > 0 ? counters_.positive_world_epoch : 0;
    if (id > 0)
        ++counters_.active_positive;
    else if (id < 0)
        ++counters_.active_negative;
    else
        ++counters_.active_zero;
    ++counters_.add_events;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_duplicate(std::int32_t id) noexcept {
    AcquireSRWLockExclusive(&lock_);
    ++counters_.duplicate_add_events;
    const std::size_t index = find_entry(id);
    if (index == kNoEntry || !entries_[index].active) {
        ++counters_.duplicate_add_untracked;
        counters_.coverage_ambiguous = true;
    }
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::decrement_active(std::int32_t id) noexcept {
    if (id > 0)
        --counters_.active_positive;
    else if (id < 0)
        --counters_.active_negative;
    else
        --counters_.active_zero;
}

void LifecycleObserver::record_remove_observation(
    std::int32_t id, bool present_before, bool present_after) noexcept {
    AcquireSRWLockExclusive(&lock_);
    ++counters_.remove_calls;
    if (present_after) {
        ++counters_.remove_lookup_mismatches;
        ++counters_.lookup_misses;
        counters_.coverage_ambiguous = true;
        ReleaseSRWLockExclusive(&lock_);
        return;
    }
    if (!present_before) {
        ++counters_.remove_misses;
        ReleaseSRWLockExclusive(&lock_);
        return;
    }
    const std::size_t index = find_entry(id);
    if (index == kNoEntry || !entries_[index].active) {
        ++counters_.remove_untracked_successes;
        counters_.coverage_ambiguous = true;
        ReleaseSRWLockExclusive(&lock_);
        return;
    }
    entries_[index].active = false;
    decrement_active(id);
    ++counters_.remove_events;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_positive_bulk_reset() noexcept {
    AcquireSRWLockExclusive(&lock_);
    if (counters_.positive_world_epoch ==
        std::numeric_limits<std::uint64_t>::max()) {
        mark_overflow();
    } else {
        ++counters_.positive_world_epoch;
    }
    for (Entry& entry : entries_) {
        if (entry.occupied && entry.active && entry.id > 0) entry.active = false;
    }
    counters_.active_positive = 0;
    counters_.positive_coverage_armed = true;
    ++counters_.positive_bulk_resets;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_tick_completion() noexcept {
    AcquireSRWLockExclusive(&lock_);
    ++counters_.tick_completions;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_draw_lookup(std::int32_t id) noexcept {
    AcquireSRWLockExclusive(&lock_);
    const std::size_t index = find_entry(id);
    const bool missing = index == kNoEntry || !entries_[index].active;
    if (id > 0 && !counters_.positive_coverage_armed) {
        ++counters_.pre_reset_positive_draws;
        if (missing) ++counters_.pre_reset_positive_draw_misses;
    } else if (id > 0) {
        ++counters_.post_reset_positive_draws;
        if (missing) {
            ++counters_.drawn_positive_id_misses;
            counters_.coverage_ambiguous = true;
        }
    } else if (id < 0) {
        ++counters_.negative_draws_observed;
        if (missing) ++counters_.negative_draw_misses;
    } else {
        ++counters_.zero_draws_observed;
        if (missing) ++counters_.zero_draw_misses;
    }
    if (missing) {
        ++counters_.lookup_misses;
    }
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_lookup_mismatch() noexcept {
    AcquireSRWLockExclusive(&lock_);
    ++counters_.lookup_misses;
    counters_.coverage_ambiguous = true;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_tick_nested_mutation() noexcept {
    AcquireSRWLockExclusive(&lock_);
    ++counters_.tick_nested_mutations;
    ReleaseSRWLockExclusive(&lock_);
}

void LifecycleObserver::record_mutation_reentrant_event() noexcept {
    AcquireSRWLockExclusive(&lock_);
    ++counters_.mutation_reentrant_events;
    counters_.coverage_ambiguous = true;
    ReleaseSRWLockExclusive(&lock_);
}

bool LifecycleObserver::active_identity(
    std::int32_t id, LifecycleIdentity& identity) const noexcept {
    AcquireSRWLockShared(&lock_);
    const std::size_t index = find_entry(id);
    const bool found = index != kNoEntry && entries_[index].active;
    if (found) {
        identity.id = entries_[index].id;
        identity.generation = entries_[index].generation;
        identity.positive_world_epoch = entries_[index].positive_world_epoch;
    }
    ReleaseSRWLockShared(&lock_);
    return found;
}

LifecycleProbeSnapshot LifecycleObserver::snapshot() const noexcept {
    AcquireSRWLockShared(&lock_);
    const LifecycleProbeSnapshot result = counters_;
    ReleaseSRWLockShared(&lock_);
    return result;
}

LifecycleObserver& lifecycle_observer() noexcept {
    static LifecycleObserver observer;
    return observer;
}

const char* lifecycle_validation_name(LifecycleValidation validation) noexcept {
    switch (validation) {
    case LifecycleValidation::disabled: return "disabled";
    case LifecycleValidation::active: return "active";
    case LifecycleValidation::active_without_draw_oracle:
        return "active_without_draw_oracle";
    case LifecycleValidation::signature_mismatch: return "signature_mismatch";
    case LifecycleValidation::allocation_failure: return "allocation_failure";
    case LifecycleValidation::patch_failure: return "patch_failure";
    case LifecycleValidation::contract_fault: return "contract_fault";
    default: return "unknown";
    }
}

bool format_lifecycle_probe_json(
    char* output, std::size_t capacity,
    const LifecycleProbeSnapshot& value) noexcept {
    if (!output || capacity == 0) return false;
    const int written = std::snprintf(
        output, capacity,
        "{\"validation\":\"%s\",\"requested\":%s,\"observer_active\":%s,"
        "\"hooks_installed\":%s,\"draw_oracle_validated\":%s,"
        "\"fail_closed\":%s,\"coverage_ambiguous\":%s,\"overflowed\":%s,"
        "\"positive_coverage_armed\":%s,"
        "\"negative_membership_mode\":\"%s\",\"zero_membership_mode\":\"%s\","
        "\"positive_world_epoch\":%llu,\"last_generation\":%llu,"
        "\"active_positive\":%llu,\"active_negative\":%llu,\"active_zero\":%llu,"
        "\"add_events\":%llu,\"duplicate_add_events\":%llu,"
        "\"duplicate_add_untracked\":%llu,\"remove_calls\":%llu,"
        "\"remove_events\":%llu,\"remove_misses\":%llu,"
        "\"remove_untracked_successes\":%llu,\"remove_lookup_mismatches\":%llu,"
        "\"positive_bulk_resets\":%llu,\"tick_completions\":%llu,"
        "\"lookup_misses\":%llu,\"pre_reset_positive_draws\":%llu,"
        "\"pre_reset_positive_draw_misses\":%llu,"
        "\"post_reset_positive_draws\":%llu,"
        "\"drawn_positive_id_misses\":%llu,\"negative_draws_observed\":%llu,"
        "\"negative_draw_misses\":%llu,\"zero_draws_observed\":%llu,"
        "\"zero_draw_misses\":%llu,\"id_reuses\":%llu,"
        "\"tick_nested_mutations\":%llu,"
        "\"mutation_reentrant_events\":%llu,"
        "\"hook_validation_failures\":%llu,"
        "\"overflows\":%llu}",
        lifecycle_validation_name(value.validation),
        value.requested ? "true" : "false",
        value.observer_active ? "true" : "false",
        value.hooks_installed ? "true" : "false",
        value.draw_oracle_validated ? "true" : "false",
        value.fail_closed ? "true" : "false",
        value.coverage_ambiguous ? "true" : "false",
        value.overflowed ? "true" : "false",
        value.positive_coverage_armed ? "true" : "false",
        value.negative_membership_observed_only ? "observed_only" : "complete",
        value.zero_membership_observed_only ? "observed_only" : "complete",
        static_cast<unsigned long long>(value.positive_world_epoch),
        static_cast<unsigned long long>(value.last_generation),
        static_cast<unsigned long long>(value.active_positive),
        static_cast<unsigned long long>(value.active_negative),
        static_cast<unsigned long long>(value.active_zero),
        static_cast<unsigned long long>(value.add_events),
        static_cast<unsigned long long>(value.duplicate_add_events),
        static_cast<unsigned long long>(value.duplicate_add_untracked),
        static_cast<unsigned long long>(value.remove_calls),
        static_cast<unsigned long long>(value.remove_events),
        static_cast<unsigned long long>(value.remove_misses),
        static_cast<unsigned long long>(value.remove_untracked_successes),
        static_cast<unsigned long long>(value.remove_lookup_mismatches),
        static_cast<unsigned long long>(value.positive_bulk_resets),
        static_cast<unsigned long long>(value.tick_completions),
        static_cast<unsigned long long>(value.lookup_misses),
        static_cast<unsigned long long>(value.pre_reset_positive_draws),
        static_cast<unsigned long long>(value.pre_reset_positive_draw_misses),
        static_cast<unsigned long long>(value.post_reset_positive_draws),
        static_cast<unsigned long long>(value.drawn_positive_id_misses),
        static_cast<unsigned long long>(value.negative_draws_observed),
        static_cast<unsigned long long>(value.negative_draw_misses),
        static_cast<unsigned long long>(value.zero_draws_observed),
        static_cast<unsigned long long>(value.zero_draw_misses),
        static_cast<unsigned long long>(value.id_reuses),
        static_cast<unsigned long long>(value.tick_nested_mutations),
        static_cast<unsigned long long>(value.mutation_reentrant_events),
        static_cast<unsigned long long>(value.hook_validation_failures),
        static_cast<unsigned long long>(value.overflows));
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

} // namespace owned
