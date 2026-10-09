#pragma once
#include "RPGSystemConsts.h"
#include <cstring>
#include <cstdio>
#include <string>

// Native LS39454a3: CheckShield11697, UseArmorSpells11525,
// UseCurrentWeaponSpell11467, GetCastPointNameDual14211, wand19533.
namespace ModItemSpells {
inline bool KeepContinuousWeapon(bool current, bool hasCurrent, bool twoHanded, bool activeLeft)
{
    return current || (hasCurrent && !twoHanded && activeLeft);
}
inline bool IsActiveShield(bool hasCurrent, int leftSlot, int slot, int type)
{
    return hasCurrent && leftSlot >= 0 && leftSlot == slot && type == RPGWST_HANDS1;
}
inline bool FiltersExistingEffect(int spellID, bool shield)
{
    switch (spellID) {
    case SPELL_ID_PROT_FIRE: case SPELL_ID_PROT_ELECTRIC: case SPELL_ID_PROT_ACID:
    case SPELL_ID_ANTIMAGIC: case SPELL_ID_SHRINK: case SPELL_ID_ENLARGE:
    case SPELL_ID_STRENGTHEN: case SPELL_ID_WEAKEN: case SPELL_ID_SPEED:
    case SPELL_ID_SLOW: case SPELL_ID_STUN:
        return true;
    case SPELL_ID_REGENERATION:
        return shield;
    default:
        return false;
    }
}
template<class EffectAt>
inline bool Suppressed(int spellID, float power, bool shield, int count, EffectAt at)
{
    if (!FiltersExistingEffect(spellID, shield)) return false;
    for (int i = 0; i < count; ++i) {
        const auto& effect = at(i);
        // This is the native comparison, including its direction.
        if (effect.nSpellID == spellID && effect.fpPower <= power) return true;
    }
    return false;
}
inline bool TriggerPhase(int spellID, bool before, bool shield, int healAfterHit)
{
    if (spellID != SPELL_ID_HEALING) return before;
    return !shield && healAfterHit == 1 ? before : !before;
}
inline bool CanTrigger(bool trigger, float mana, float cost, bool suppressed)
{
    return trigger && mana >= cost && !suppressed;
}
inline bool WeaponHealingTargetsSelf(const char* name)
{
    return name && std::strcmp(name, "Healing") == 0;
}
inline bool WandLacksMana(bool wand, bool hasSpell, float cost, float mana)
{
    return wand && hasSpell && cost > mana;
}
inline std::string CastPointNode(bool validShape, int type, int texture, bool left)
{
    if (!validShape) return left ? "lh3" : "baserh3";
    const char* base = nullptr;
    switch (type) {
    case RPGWST_SWORD: base = "basesword"; break;
    case RPGWST_AXE: base = "baseaxe"; break;
    case RPGWST_DAGGER: base = "basedagger"; break;
    case RPGWST_SPEAR: base = "basepike"; break;
    case RPGWST_HAMMER: base = "baseclub"; break;
    case RPGWST_BOW: return "basearrow00";
    case RPGWST_CROSSBOW: return "basearrow01";
    case RPGWST_HANDS4: base = "basedsword"; break;
    case RPGWST_HANDS5: base = "basedaxe"; break;
    case RPGWST_HANDS2: base = "basedpike"; break;
    case RPGWST_HANDS6: base = "basedclub"; break;
    case RPGWST_HANDS3: base = "basestaff"; break;
    case RPGWST_HANDS7: base = "basedstaff"; break;
    default: return left ? "rh3" : "lh3";
    }
    char number[32]; std::snprintf(number, sizeof(number), "%.2d", texture);
    return std::string(left ? "l." : "") + base + number;
}
}
