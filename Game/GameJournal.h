#pragma once
#include "../external/owned-renderer/Source/runtime_diagnostics.h"
#include <string>
#ifndef GAME_JOURNAL_STANDALONE
#include <list>
#endif

// Passive event observers; memory snapshots read existing providers on the game thread.
// No observer mutates the game world.
class CUnit;
class CMapObject;
class CItem;
namespace CrashEvidence { struct Details; }
namespace GameJournal {
// Scalars are copied into bounded journal-owned aggregates. No game objects are retained.
void Damage(int attacker, int target, const char* attackerPrototype, const char* targetPrototype,
            bool squad, float amount, int outcome, int critical = -1) noexcept;
void Healing(int actor, int target, const char* prototype, float amount) noexcept;
void CombatBoundary(const char* reason, int dead = -1) noexcept;
void SetCallback(const char* name) noexcept;
const char* CurrentSource() noexcept;
class Callback {
    std::string previous;
public:
    Callback() noexcept;
    ~Callback() noexcept;
};
class Actor {
    int previous;
public:
    explicit Actor(int id) noexcept;
    ~Actor() noexcept;
};
const char* CampMode(int value) noexcept;
const char* ItemPart(int value) noexcept;
#ifndef GAME_JOURNAL_STANDALONE
void ObserveDamage(CUnit* attacker, CUnit* target, float amount, int outcome, int critical) noexcept;
void ObserveHeal(CUnit* target, float amount) noexcept;
struct SpellObservation {
    int actor = -1, target = -1;
    char prototype[128] = "unknown";
    bool squad = false;
};
// Capture only at a live-object call site, before callbacks may remove the objects.
SpellObservation CaptureSpell(CMapObject* actor, CMapObject* target) noexcept;
void ObserveSpell(const SpellObservation& captured, const char* code, int phase, int result, float x, float y, float z, bool combat = true) noexcept;
void ObserveObject(CMapObject* object, const char* action) noexcept;
void ObserveSelection(const std::list<int>& ids) noexcept;
void TradeReset(const void* screen) noexcept;
void TradeQuote(const void* screen, CItem* item, const char* direction, int price) noexcept;
void TradeCommit(const void* screen, int shop, int total) noexcept;
#endif
void MemorySnapshot(const char* reason, const char* allocationSource = "") noexcept;
void MemoryFrame(unsigned long now) noexcept;
void AllocationFailure(const char* source) noexcept;
void AllocationFailureInCatch(const char* source) noexcept; // Only inside a catch block.
void MemoryErrorText(const char* text) noexcept;
#if defined(__SANITIZE_ADDRESS__)
void ASanReport(const wchar_t* path, unsigned long error, const void* pc, unsigned long long count, bool repeat) noexcept;
#endif
void Start() noexcept;
void Stop() noexcept;
void Tick(unsigned long tick) noexcept;
void Event(const char* category, const char* event, const char* format = "", ...) noexcept;
// Ordinary warnings are queued and repeated texts are counted on the writer cadence.
void Notification(const char* type, const char* text, const char* extra) noexcept;
// One summary per model/request at session end; never emits a per-request warning.
enum class AnimationGapResult { Ordinary, Missing, SkipTransition, OrdinaryIdle };
void MissingAnimation(const char* model, unsigned long request, AnimationGapResult result) noexcept;
std::string Quote(const char* cp1251) noexcept;
void Flush() noexcept; // Wake the writer; no disk I/O on the caller thread.
void FlushCritical() noexcept; // Bounded lock wait for fatal errors/save boundaries.
void Crash(const wchar_t* dump, unsigned long code, const CrashEvidence::Details* details = nullptr) noexcept;
void Variable(const void* set, const char* name, bool present, float oldValue,
              float newValue, bool deleting) noexcept;
void Count(const char* category, const char* event, const char* key) noexcept;
class Source {
    const char* previous;
public:
    explicit Source(const char* name) noexcept;
    ~Source() noexcept;
};
class Operation {
    const char* category;
    const char* name;
    unsigned long started;
    bool success;
public:
    Operation(const char* category, const char* name, const char* detail) noexcept;
    void Result(bool value) noexcept { success = value; }
    ~Operation() noexcept;
};
#ifdef GAME_JOURNAL_STANDALONE
void TestStart(const wchar_t* directory, unsigned long limit = 20 * 1024 * 1024) noexcept;
void TestDrain() noexcept;
void TestCombatClock(unsigned long value) noexcept;
#endif
}
