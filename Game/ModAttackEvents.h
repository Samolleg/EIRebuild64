#pragma once
#include "GameJournal.h"
#include "Player.h"
#include "Unit.h"
#include "WorldServer.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace ModAttackEvents {
constexpr std::uint32_t MaxSequence = 0xffffff;

struct Actor {
    int id = 0;
    ObjectId incarnation = 0;
    bool operator==(const Actor& other) const { return id == other.id && incarnation == other.incarnation; }
};
inline Actor Identify(const CUnit& unit) { return {unit.GetID(), unit.GetRTID()}; }
inline bool Valid(const CUnit* unit) { return unit && !unit->IsInvalid() && unit->GetID() != 0; }

struct Event {
    std::uint32_t sequence = 0;
    Actor attacker, target;
    int tick = 0;
    bool damage = false, physical = false;
    float distance = 0;
};
struct State {
    std::array<Event, 4096> entries{};
    std::size_t next = 0, count = 0;
    std::uint32_t sequence = 0;
    std::uint64_t generation = 0;
    int lastTick = -1;
    const CWorldServer* world = nullptr;
    ObjectId player = 0;

    void ClearEntries() { next = count = 0; }
    void Reset() {
        ClearEntries(); sequence = 0; lastTick = -1; world = nullptr; player = 0; ++generation;
    }
    static const std::string& SequenceName() {
        // Reuse the key: unordered_map<string,...> must not allocate a temporary on every hit.
        static const std::string name = "AttackEvents.Sequence";
        return name;
    }
    CPlayer* Sync(CWorldServer& current) {
        auto* owner = current.GetPlayer(0);
        const ObjectId ownerId = owner ? owner->GetRTID() : 0;
        if (world != &current || player != ownerId) {
            ClearEntries(); world = &current; player = ownerId; lastTick = -1;
        }
        std::uint32_t saved = sequence;
        if (owner) {
            const auto& vars = owner->party.vars;
            auto found = vars.find(SequenceName());
            const float value = found == vars.end() ? 0.f : found->second;
            saved = std::isfinite(value) && value >= 0 && value <= MaxSequence ? std::uint32_t(value) : 0;
        }
        const int tick = current.GetSegmentTimeCounter();
        if (tick < lastTick || saved != sequence) ClearEntries();
        lastTick = tick; sequence = saved;
        return owner;
    }
    void Record(CWorldServer& current, Event event) {
        auto* owner = Sync(current);
        sequence = sequence >= MaxSequence ? 1 : sequence + 1;
        if (sequence == 1) ClearEntries();
        if (owner) {
            auto& vars = owner->party.vars;
            auto found = vars.find(SequenceName());
            GameJournal::Variable(&vars, SequenceName().c_str(), found != vars.end(),
                                  found == vars.end() ? 0.f : found->second, float(sequence), false);
            // Same nonzero GS write as SetVar, without per-delivery key construction.
            if (found == vars.end()) vars.emplace(SequenceName(), float(sequence));
            else found->second = float(sequence);
        }
        event.sequence = sequence; event.tick = current.GetSegmentTimeCounter();
        entries[next] = event;
        next = (next + 1) % entries.size();
        if (count < entries.size()) ++count;
    }
};
inline State& Journal() { static State state; return state; }
inline void Reset() { Journal().Reset(); }

// Simulation-thread context for a spell whose AI-facing attacker is deliberately hidden.
// It names the exact portion and recipient, so nested unrelated damage cannot inherit it.
class AuthorScope {
    const SRPGAttackPortion* portion;
    const CUnit* target;
    CUnit* author;
    AuthorScope* previous;
    static AuthorScope*& Current() { static AuthorScope* scope = nullptr; return scope; }
public:
    AuthorScope(const SRPGAttackPortion& attack, const CUnit& recipient, CUnit* caster)
        : portion(&attack), target(&recipient), author(caster), previous(Current()) { Current() = this; }
    ~AuthorScope() { Current() = previous; }
    AuthorScope(const AuthorScope&) = delete;
    AuthorScope& operator=(const AuthorScope&) = delete;
    static CUnit* Resolve(const SRPGAttackPortion& attack, const CUnit* recipient, CUnit* source) {
        if (source) return source;
        auto* scope = Current();
        return scope && scope->portion == &attack && scope->target == recipient ? scope->author : nullptr;
    }
};

struct Delivery {
    Event event;
    std::uint64_t generation = 0;
    float before = 0;
    bool valid = false;
};
inline Delivery Begin(const SRPGAttackPortion& portion, CUnit* source, CUnit* target, float before) {
    Delivery delivery;
    auto* attacker = AuthorScope::Resolve(portion, target, source);
    if (!Valid(attacker) || attacker->IsDead() || !Valid(target) || target->IsDead() || !std::isfinite(before))
        return delivery;
    delivery.valid = true; delivery.generation = Journal().generation; delivery.before = before;
    delivery.event.attacker = Identify(*attacker); delivery.event.target = Identify(*target);
    delivery.event.physical = portion.fpAttacks[0] > 0 || portion.fpAttacks[1] > 0 || portion.fpAttacks[2] > 0;
    const auto& a = attacker->GetCP(); const auto& t = target->GetCP();
    delivery.event.distance = std::hypot(a.x - t.x, a.y - t.y);
    return delivery;
}
inline void Complete(CWorldServer& world, const Delivery& delivery, float after) {
    if (!delivery.valid || delivery.generation != Journal().generation || !std::isfinite(after)) return;
    auto event = delivery.event;
    event.damage = after < delivery.before;
    Journal().Record(world, event);
}

inline float Query(CWorldServer& world, int command, CUnit* first, CUnit* second, float requested) {
    if (!std::isfinite(requested) || requested < 0) return 0;
    const bool radius = command == 275 || command == 285;
    if (!radius && (requested > 2 || requested != std::floor(requested))) return 0;
    if (command == 295) std::swap(first, second);
    if (!Valid(first) || !Valid(second)) return 0; // Dead, still present actors remain queryable.
    auto& journal = Journal(); journal.Sync(world);
    const Actor attacker = Identify(*first), target = Identify(*second);
    for (std::size_t n = 0; n < journal.count; ++n) {
        const auto& event = journal.entries[(journal.next + journal.entries.size() - 1 - n) % journal.entries.size()];
        if (!(event.attacker == attacker) || !(event.target == target)) continue;
        if (radius) {
            if ((command == 285 && !event.physical) || !std::isfinite(event.distance) || event.distance > requested) continue;
        } else if (requested != 2 && event.damage != (requested == 1)) continue;
        if (radius || command == 294) return float(event.sequence);
        const auto age = std::int64_t(world.GetSegmentTimeCounter()) - event.tick;
        return age >= 0 && age <= 2 ? 1.f : 0.f;
    }
    return 0;
}
} // namespace ModAttackEvents
