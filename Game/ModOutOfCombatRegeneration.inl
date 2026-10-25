#include "ModOutOfCombatRegeneration.h"
#include <unordered_map>

namespace ModOutOfCombatRegeneration {
namespace {
constexpr int CheckSegments = 75;
constexpr int CooldownSegments = 900;
constexpr float EnemyRadius = 25.0f;
struct Entry {
    CUnit* unit = nullptr;
    CUnitServer* server = nullptr;
    CPlayer* owner = nullptr;
    int playerNumber = -1;
    long long nextCheck = 0, lastCombat = 0;
    int checkedAt = -1;
    bool inCombat = false, hadCombat = false, safe = false;
};
std::unordered_map<ObjectId, Entry>& Entries() {
    // Process lifetime: global world/unit destructors also call Reset/Forget.
    // Entries themselves are cleared with each world and are never serialized.
    static auto* entries = new std::unordered_map<ObjectId, Entry>;
    return *entries;
}
bool SameUnit(const Entry& entry, CUnit* unit) {
    return entry.unit == unit && entry.server == unit->GetUnitSpecific() &&
        entry.owner == unit->GetOwner() && entry.playerNumber == unit->GetPlayerNum();
}
bool InCombat(CUnit* unit, CUnitServer* server) {
    CPlayer* player = theWorldServer.GetPlayerManager() ? theWorldServer.GetPlayer(0) : nullptr;
    if (player && player == unit->GetOwner()) {
        auto* specific = dynamic_cast<CPlayerServer*>(player->GetPlayerSpecific());
        return !specific || specific->dangerState == WDS_COMBAT;
    }
    return server->IsAggressive();
}
bool HasNearbyEnemy(CUnit* unit) {
    CUnitList nearby;
    // The spatial query is strict; include the boundary before applying <= 625.
    theWorldServer.aiMap.GetUnitsNear(unit->GetCP(), nearby,
        std::nextafter(EnemyRadius, (std::numeric_limits<float>::infinity)()));
    for (POSITION pos = nearby.GetHeadPosition(); pos;) {
        CUnit* enemy = nearby.GetNext(pos);
        if (enemy == unit || !IsLivingHostile(enemy, unit->GetPlayerNum())) continue;
        if (distanceXY2(enemy->GetCP(), unit->GetCP()) <= EnemyRadius * EnemyRadius) return true;
    }
    return false;
}
}
bool IsLivingHostile(CUnit* candidate, int playerNumber) {
    return candidate && !candidate->IsInvalid() && !candidate->IsUnreferenced() &&
        !candidate->IsDead() && playerNumber >= 0 && playerNumber < 16 &&
        candidate->IsEnemy(playerNumber);
}
void Reset() { Entries().clear(); }
void Forget(CUnitServer* server) {
    auto& entries = Entries();
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->second.server == server) it = entries.erase(it);
        else ++it;
    }
}
void Update(CUnit* unit) {
    if (!unit || !theWorldServer.IsMapLoaded()) return;
    const int mapId = unit->GetID();
    const ObjectId id = unit->GetRTID();
    auto* server = dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if (!server || unit->IsInvalid() || unit->IsUnreferenced() || unit->IsDead() ||
        mapId == 666666 || mapId == 640706) { Entries().erase(id); return; }
    const int tick = theWorldServer.GetSegmentTimeCounter();
    Entry& entry = Entries()[id];
    if (!SameUnit(entry, unit)) {
        entry = Entry();
        entry.unit = unit;
        entry.server = server;
        entry.owner = unit->GetOwner();
        entry.playerNumber = unit->GetPlayerNum();
        const unsigned phase = static_cast<unsigned>(mapId) % CheckSegments;
        const unsigned nowPhase = static_cast<unsigned>(tick) % CheckSegments;
        entry.nextCheck = static_cast<long long>(tick) + (phase + CheckSegments - nowPhase) % CheckSegments;
    }
    const bool combat = InCombat(unit, server);
    if (combat || entry.inCombat) {
        entry.lastCombat = tick;
        entry.hadCombat = true;
        entry.safe = false;
    }
    entry.inCombat = combat;
    if (tick < entry.nextCheck) return;
    entry.nextCheck = static_cast<long long>(tick) + CheckSegments;
    entry.checkedAt = tick;
    entry.safe = !combat &&
        (!entry.hadCombat || static_cast<long long>(tick) - entry.lastCombat >= CooldownSegments) &&
        !HasNearbyEnemy(unit);
}
bool IsSafe(CUnit* unit) {
    if (!unit || !theWorldServer.IsMapLoaded() || !theWorldServer.GetMapObjectManager()) return false;
    // UI objects are client-side: resolve the corresponding authoritative instance.
    unit = theWorldServer.GetUnit(unit->GetID());
    if (!unit || unit->IsInvalid() || unit->IsUnreferenced() || unit->IsDead()) return false;
    const auto it = Entries().find(unit->GetRTID());
    if (it == Entries().end() || !SameUnit(it->second, unit)) return false;
    const Entry& entry = it->second;
    const long long age = static_cast<long long>(theWorldServer.GetSegmentTimeCounter()) - entry.checkedAt;
    return entry.safe && entry.checkedAt >= 0 && age >= 0 && age < CheckSegments;
}
float GetMultiplier(CUnit* unit) { return IsSafe(unit) ? 5.0f : 1.0f; }
}
