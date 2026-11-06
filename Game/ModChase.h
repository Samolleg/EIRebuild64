#pragma once
#include "ModInvincible.h"
#include "UnitServer.h"
#include "MapObjectServer.h"
#include <cfloat>

// Dejavu plugin 421604cc78b4: synchronous admission, MOB-owned progression.
namespace ModChase {
inline float Read(CWorldServer& world, const char* key) {
    auto* player = world.GetPlayer(0);
    return player ? player->party.vars.GetVar(key) : 0;
}
inline void Set(CWorldServer& world, const char* key, float value) {
    if (auto* player = world.GetPlayer(0)) player->party.vars.SetVar(key, value);
}
inline bool Active(CWorldServer& world) {
    if (!ModRuntime::Current()) return false;
    const char* name = ModInvincible::Map(world);
    if (_stricmp(name, "zone19") && _stricmp(name, "zone19.mob") &&
        _stricmp(name, "gz19h") && _stricmp(name, "gz19h.mob")) return false;
    return Read(world, "Z19.Enabled") == 1 && Read(world, "q.gz19h.q72h") == 1;
}
inline bool Hero(CWorldServer& world, CUnit* unit) {
    auto* player = world.GetPlayer(0);
    return player && unit && unit->GetOwner() == player;
}
inline float Per(CWorldServer& world, CUnit& unit, const char* field) {
    char key[96];
    std::snprintf(key, sizeof(key), "Z19.H%u.%s", unsigned(unit.GetID()), field);
    return Read(world, key);
}
inline void Per(CWorldServer& world, CUnit& unit, const char* field, float value) {
    char key[96];
    std::snprintf(key, sizeof(key), "Z19.H%u.%s", unsigned(unit.GetID()), field);
    Set(world, key, value);
}
inline void Paralyze(CUnit* unit) {
    if (!ModInvincible::UnitValid(unit, true) || (ModInvincible::Mask(unit) & ModInvincible::Bad) ||
        unit->CheckFlag(UNIT_STUN)) return;
    CSpellEffect effect;
    effect.nSpellID = SPELL_ID_STUN;
    effect.nDuration = 54;
    effect.fpPower = 1;
    effect.bQuiet = true;
    unit->AddSpellEffect(effect);
    unit->UpdateFrame();
}
inline bool Block(CWorldServer& world, CUnit* target, CUnit* author, const SRPGAttackPortion& portion) {
    if (!Active(world) || !ModInvincible::UnitValid(target, true)) return false;
    const int id = target->GetID();
    if (id != 1000105 && id != 997569) return false;
    bool damaging = false;
    for (float weight : portion.fpAttacks) damaging = damaging || weight > 0;
    if (!damaging || !std::isfinite(portion.fpDamage) || portion.fpDamage <= 0) return false;
    const bool hero = ModInvincible::UnitValid(author, true) && Hero(world, author);
    bool blocked = false;
    if (id == 997569) {
        blocked = Read(world, "Z19.GeneralReleased") != 1;
        if (blocked && hero) Set(world, "Z19.GeneralRequest", 1);
    } else if (Read(world, "PE19ElderDead") != 1 && Read(world, "PE19ElderEscaped") != 1) {
        if (hero) {
            const int now = world.GetSegmentTimeCounter();
            Set(world, "Z19.ErfarContactUntil", float(now + 36));
            if (Per(world, *author, "RetaliateNext") <= now) {
                Per(world, *author, "RetaliateNext", float(now + 108));
                Paralyze(author);
                Set(world, "Z19.Retaliations", Read(world, "Z19.Retaliations") + 1);
            }
            const auto& a = author->GetCP();
            const auto& t = target->GetCP();
            const float distance = std::hypot(a.x - t.x, a.y - t.y);
            blocked = !std::isfinite(distance) || distance > 35.f || Read(world, "Z19.ErfarVisible") != 1;
        } else if (!ModInvincible::UnitValid(author)) blocked = true;
    }
    if (blocked) {
        const char* key = id == 1000105 ? "Z19.ErfarBlocked" : "Z19.GeneralBlocked";
        Set(world, key, Read(world, key) + 1);
    }
    return blocked;
}
inline bool Trap(CWorldServer& world, CUnit* unit, float pure, float elemental, float type) {
    Set(world, "Z19.LastTrapApplied", 0);
    if (!Active(world) || !ModInvincible::UnitValid(unit, true) || !std::isfinite(pure) ||
        !std::isfinite(elemental) || !std::isfinite(type) || pure < 0 || elemental < 0 ||
        pure + elemental > .950001f || type < 0 || type > 6 || type != std::floor(type)) return false;
    auto* server = dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if (!server || ModInvincible::World(unit) != &world) return false;
    const float maximum = server->GetMapObjectServer()->GetMaxHP();
    if (!std::isfinite(maximum) || maximum <= 0) return false;
    const bool hero = Hero(world, unit);
    const int now = world.GetSegmentTimeCounter();
    if (hero && Per(world, *unit, "TrapGrace") > now) return false;
    if (hero && pure + elemental >= .35f) Per(world, *unit, "TrapGrace", float(now + 36));
    const float before = server->GetMapObjectServer()->GetCurrentHP();
    if (elemental > 0 && ModInvincible::UnitValid(unit, true)) {
        SRPGAttackPortion portion;
        portion.Clear();
        portion.fpAttacks[int(type)] = 1;
        portion.fpToHit = FLT_MAX; // Scripted contact, not a second hit roll.
        portion.fpDamage = maximum * elemental;
        portion.hl = RPGHL_ALL;
        portion.bAreaEffect = TRUE;
        portion.fBackStabMultiplier = 1;
        portion.bCalledShot = true;
        server->ProcessAttackPortion(&portion, nullptr, nullptr);
        if (ModInvincible::UnitValid(unit, true) && server->GetMapObjectServer()->GetCurrentHP() <= 0)
            server->ApplyDirectDamage(1);
    }
    // On-contact healing from the elemental delivery must finish before guaranteed loss.
    if (pure > 0 && ModInvincible::UnitValid(unit, true)) server->ApplyDirectDamage(maximum * pure);
    const bool applied = server->GetMapObjectServer()->GetCurrentHP() < before;
    Set(world, "Z19.LastTrapApplied", applied ? 1.f : 0.f);
    if (applied) {
        Set(world, "Z19.TrapHits", Read(world, "Z19.TrapHits") + 1);
        if (hero) Per(world, *unit, "LastTrap", float(now));
    }
    return applied;
}
} // namespace ModChase
