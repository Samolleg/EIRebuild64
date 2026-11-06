#pragma once
#include "Unit.h"
#include "Player.h"
#include "WorldServer.h"
#include "ModRuntime.h"
#include "SpellSystem.h"
#include "SpellDatabase.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Dejavu plugin 421604cc78b4: saved, map-local channel and status policy.
namespace ModInvincible {
constexpr unsigned Physical = 7, Magic = 0x38, Direct = 0x40, Bad = 0x80, Buffs = 0x100;
constexpr unsigned All = 0xff, Valid = 0x1ff; // All deliberately excludes Buffs.

inline bool UnitValid(const CUnit* unit, bool alive = false) {
    return unit && !unit->IsInvalid() && (!alive || !unit->IsDead());
}
inline CWorldServer* World(const CUnit* unit) {
    return UnitValid(unit) && unit->GetManager() ? dynamic_cast<CWorldServer*>(unit->GetWorld()) : nullptr;
}
inline const char* Map(const CWorldServer& world) {
    const char* name = world.szMobFileName;
    if (!name || !*name) return "unloaded";
    const char* slash = strrchr(name, '\\');
    const char* other = strrchr(name, '/');
    if (other && (!slash || other > slash)) slash = other;
    return slash ? slash + 1 : name;
}
inline std::string Key(const CWorldServer& world, const CUnit& unit) {
    char key[160];
    std::snprintf(key, sizeof(key), "Invincible.%.80s.%u", Map(world), unsigned(unit.GetID()));
    return key;
}
inline unsigned Mask(const CUnit* unit) {
    if (!ModRuntime::Current()) return 0;
    auto* world = World(unit);
    auto* player = world ? world->GetPlayer(0) : nullptr;
    if (!player) return 0;
    const float value = player->party.vars.GetVar(Key(*world, *unit).c_str());
    return std::isfinite(value) && value >= 0 && value <= Valid && value == std::floor(value) ? unsigned(value) : 0;
}
inline unsigned Type(const char* key) {
    if (!key) return 0;
    static const char* names[] = {"piercing", "slashing", "crushing", "fire", "water", "air", "script", "effects", "buffs"};
    for (unsigned i = 0; i < 9; ++i) if (!_stricmp(key, names[i])) return 1u << i;
    if (!_stricmp(key, "physical")) return Physical;
    if (!_stricmp(key, "magic")) return Magic;
    if (!_stricmp(key, "all")) return All;
    return 0;
}
inline bool Change(CUnit* unit, const char* name, float enabled) {
    if (!UnitValid(unit, true) || !std::isfinite(enabled) || (enabled != 0 && enabled != 1)) return false;
    const unsigned type = Type(name);
    auto* world = World(unit);
    auto* player = world ? world->GetPlayer(0) : nullptr;
    if (!type || !player) return false;
    const unsigned mask = Mask(unit);
    player->party.vars.SetVar(Key(*world, *unit).c_str(), float(enabled == 1 ? mask | type : mask & ~type));
    return true;
}

// Keep the portion's identity: prepared weapon/critical/skill records use its address.
// Restore only the policy-owned fields; native hit/history mutations remain native.
class ScopedChannels {
    SRPGAttackPortion& portion;
    float weights[RPG_NUM_ATTACK_TYPES];
public:
    ScopedChannels(SRPGAttackPortion& attack, unsigned mask) : portion(attack) {
        for (unsigned i = 0; i < RPG_NUM_ATTACK_TYPES; ++i) {
            weights[i] = portion.fpAttacks[i];
            if (mask & (1u << i)) portion.fpAttacks[i] = 0;
        }
    }
    ~ScopedChannels() { for (unsigned i = 0; i < RPG_NUM_ATTACK_TYPES; ++i) portion.fpAttacks[i] = weights[i]; }
    ScopedChannels(const ScopedChannels&) = delete;
    ScopedChannels& operator=(const ScopedChannels&) = delete;
};

inline int EffectKind(unsigned id) {
    if (id <= 8 || id == 40 || id == 41 || id >= unsigned(theSpellDatabase.spells.GetNumEntries())) return 0;
    const auto* prototype = theSpellDatabase.spells[int(id)];
    if (!prototype) return 0;
    switch (id) { case 19: case 23: case 25: case 30: case 32: case 34: case 36: case 38: case 50: return -1; }
    if (!prototype->szCodeName.CompareNoCase("crippled") || !prototype->szName.CompareNoCase("crippled")) return -1;
    const unsigned targeting = prototype->dwTargeting & 3;
    if (targeting == 2) return -1;
    if (targeting == 1) return 1;
    if (id < 45) {
        CSpell spell;
        spell.SetSubtypeID(id << 16);
        if (spell.IsOffensive()) return -1;
    }
    return 1;
}
inline bool AllowsEffect(CMapObject& object, unsigned id) {
    if (object.WhoAmI() != HPC_UNIT) return true;
    auto* unit = static_cast<CUnit*>(&object);
    if (!UnitValid(unit, true)) return true;
    const unsigned mask = Mask(unit);
    if (!(mask & (Bad | Buffs))) return true;
    const int kind = EffectKind(id);
    return !((kind < 0 && (mask & Bad)) || (kind > 0 && (mask & Buffs)));
}
} // namespace ModInvincible
